// Кодирование EXPORT SONG: PCM WAV, AAC и ALAC в M4A, частота 48 / 44,1 кГц.
#include "song_encode.h"
#include <algorithm>
#include <cstdint>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

namespace jambo {
namespace {

void freeFrame(AVFrame* frame) { av_frame_free(&frame); }
using Frame = std::unique_ptr<AVFrame, decltype(&freeFrame)>;

AVCodecID codecId(SongFormat spec) {
    switch (spec.codec) {
    case SongCodec::wav:
        if (spec.depth == 16) return AV_CODEC_ID_PCM_S16LE;
        if (spec.depth == 24) return AV_CODEC_ID_PCM_S24LE;
        if (spec.depth == 32) return AV_CODEC_ID_PCM_F32LE;
        return AV_CODEC_ID_NONE;
    case SongCodec::aac256:
    case SongCodec::aac320: return AV_CODEC_ID_AAC;
    case SongCodec::alac: return spec.depth == 16 || spec.depth == 24 ? AV_CODEC_ID_ALAC : AV_CODEC_ID_NONE;
    }
    return AV_CODEC_ID_NONE;
}

AVSampleFormat sampleFormat(AVCodecID id, int bits) {
    switch (id) {
    case AV_CODEC_ID_AAC: return AV_SAMPLE_FMT_FLTP;
    case AV_CODEC_ID_ALAC: return bits == 16 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P;
    case AV_CODEC_ID_PCM_S16LE: return AV_SAMPLE_FMT_S16;
    case AV_CODEC_ID_PCM_S24LE: return AV_SAMPLE_FMT_S32;
    case AV_CODEC_ID_PCM_F32LE: return AV_SAMPLE_FMT_FLT;
    default: return AV_SAMPLE_FMT_NONE;
    }
}

class SongEncoder {
public:
    ~SongEncoder() {
        av_packet_free(&packet_);
        if (fifo_) av_audio_fifo_free(fifo_);
        swr_free(&converter_);
        avcodec_free_context(&encoder_);
        if (container_) {
            if (container_->pb) avio_closep(&container_->pb);
            avformat_free_context(container_);
        }
    }

    bool open(const std::string& path, SongFormat spec) {
        const auto id = codecId(spec);
        const auto* codec = avcodec_find_encoder(id);
        if (!codec || (spec.rate != 44100 && spec.rate != 48000)) return false;
        const bool wave = spec.codec == SongCodec::wav;
        if (avformat_alloc_output_context2(&container_, nullptr, wave ? "wav" : "ipod", path.c_str()) < 0)
            return false;
        encoder_ = avcodec_alloc_context3(codec);
        stream_ = avformat_new_stream(container_, nullptr);
        packet_ = av_packet_alloc();
        if (!encoder_ || !stream_ || !packet_) return false;
        encoder_->sample_rate = spec.rate;
        encoder_->sample_fmt = sampleFormat(id, spec.depth);
        encoder_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        encoder_->time_base = AVRational{1, spec.rate};
        if (id == AV_CODEC_ID_ALAC) encoder_->bits_per_raw_sample = spec.depth;
        if (id == AV_CODEC_ID_AAC) encoder_->bit_rate = spec.codec == SongCodec::aac256 ? 256000 : 320000;
        if (container_->oformat->flags & AVFMT_GLOBALHEADER) encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(encoder_, codec, nullptr) < 0) return false;
        stream_->time_base = encoder_->time_base;
        if (avcodec_parameters_from_context(stream_->codecpar, encoder_) < 0) return false;

        const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&converter_, &encoder_->ch_layout, encoder_->sample_fmt, encoder_->sample_rate,
                &stereo, AV_SAMPLE_FMT_FLTP, 48000, 0, nullptr) < 0 || swr_init(converter_) < 0)
            return false;
        frameSize_ = encoder_->frame_size > 0 ? encoder_->frame_size : 4096;
        fifo_ = av_audio_fifo_alloc(encoder_->sample_fmt, 2, frameSize_ * 2);
        if (!fifo_ || avio_open(&container_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;
        AVDictionary* options = nullptr;
        if (wave) av_dict_set(&options, "rf64", "auto", 0);
        const int header = avformat_write_header(container_, &options);
        av_dict_free(&options);
        return header >= 0;
    }

    bool write(const float* left, const float* right, int frames) {
        const uint8_t* input[] = {reinterpret_cast<const uint8_t*>(left), reinterpret_cast<const uint8_t*>(right)};
        return convert(input, frames) >= 0 && drain(false);
    }

    bool finish() {
        int frames;
        do {
            frames = convert(nullptr, 0);
            if (frames < 0 || !drain(false)) return false;
        } while (frames > 0);
        if (!drain(true) || !send(nullptr) || av_write_trailer(container_) < 0) return false;
        return avio_closep(&container_->pb) >= 0;
    }

private:
    AVFormatContext* container_ = nullptr;
    AVCodecContext* encoder_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwrContext* converter_ = nullptr;
    AVAudioFifo* fifo_ = nullptr;
    int frameSize_ = 0;
    int64_t sent_ = 0;

    Frame allocate(int count) const {
        Frame frame(av_frame_alloc(), freeFrame);
        if (frame) {
            frame->nb_samples = count;
            frame->format = encoder_->sample_fmt;
            frame->sample_rate = encoder_->sample_rate;
            if (av_channel_layout_copy(&frame->ch_layout, &encoder_->ch_layout) < 0 ||
                av_frame_get_buffer(frame.get(), 0) < 0) frame.reset();
        }
        return frame;
    }

    int convert(const uint8_t** data, int count) {
        const int size = swr_get_out_samples(converter_, count);
        if (size <= 0) return size;
        auto frame = allocate(size);
        if (!frame) return -1;
        const int n = swr_convert(converter_, frame->extended_data, size, data, count);
        if (n <= 0) return n;
        return av_audio_fifo_write(fifo_, reinterpret_cast<void**>(frame->extended_data), n) == n ? n : -1;
    }

    bool drain(bool final) {
        while (av_audio_fifo_size(fifo_) >= frameSize_ || (final && av_audio_fifo_size(fifo_) > 0)) {
            const int count = std::min(frameSize_, av_audio_fifo_size(fifo_));
            auto frame = allocate(count);
            if (!frame || av_audio_fifo_read(fifo_, reinterpret_cast<void**>(frame->extended_data), count) != count)
                return false;
            frame->pts = sent_;
            sent_ += count;
            if (!send(frame.get())) return false;
        }
        return true;
    }

    bool send(AVFrame* frame) {
        if (avcodec_send_frame(encoder_, frame) < 0) return false;
        while (true) {
            const int status = avcodec_receive_packet(encoder_, packet_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
            if (status < 0) return false;
            av_packet_rescale_ts(packet_, encoder_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;
            const int result = av_interleaved_write_frame(container_, packet_);
            av_packet_unref(packet_);
            if (result < 0) return false;
        }
    }
};

}  // namespace

bool encodeSong(const std::string& path, SongFormat format,
                const std::function<bool(const SongPcmSink&)>& writer) {
    SongEncoder encoder;
    if (!encoder.open(path, format)) return false;
    const SongPcmSink sink = [&](const float* l, const float* r, int n) { return encoder.write(l, r, n); };
    return writer(sink) && encoder.finish();
}

}  // namespace jambo
