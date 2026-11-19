#include "audio_engine.h"
#include "link_session.h"
#include <oboe/OboeExtensions.h>
#include <cmath>
#include <ctime>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "JAMbo", __VA_ARGS__)

namespace {
constexpr float kTwoPi = 6.28318530718f;
int64_t monoNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
}

bool AudioEngine::start() {
    std::lock_guard<std::mutex> g(lock_);
    wanted_ = true;
    for (int i = 0; i < 128; ++i) { gate_[i].store(false); onCount_[i].store(0); vel_[i].store(0); }
    return openLocked();
}

bool AudioEngine::openLocked() {
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setSampleRate(48000)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setUsage(oboe::Usage::Media)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    oboe::Result r = b.openStream(stream_);
    if (r != oboe::Result::OK) { LOGI("openStream: %s", oboe::convertToText(r)); return false; }
    // Два «бёрста» — рекомендованный минимум без треска.
    stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);
    sr_ = stream_->getSampleRate();
    frameCount_ = 0;
    lastClickBeat_ = -1e9;
    if (link_) link_->resetHostTime();
    tapFrame_.store(-1);
    r = stream_->requestStart();
    if (r != oboe::Result::OK) { LOGI("start: %s", oboe::convertToText(r)); return false; }
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> g(lock_);
    wanted_ = false;
    if (stream_) { stream_->stop(); stream_->close(); stream_.reset(); }
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result r) {
    // Отключили наушники / сменился выход — открываем поток заново.
    LOGI("stream closed: %s, reopening", oboe::convertToText(r));
    std::lock_guard<std::mutex> g(lock_);
    stream_.reset();
    if (wanted_) openLocked();
}

void AudioEngine::tap(int64_t touchNanos) {
    tapTouchNs_.store(touchNanos);
    tapSeq_.fetch_add(1);
}

void AudioEngine::noteOn(int note, int velocity) {
    if (note < 0 || note > 127) return;
    if (velocity == 0) { noteOff(note); return; }
    vel_[note].store(velocity / 127.0f);
    gate_[note].store(true);
    onCount_[note].fetch_add(1);
}

void AudioEngine::noteOff(int note) {
    if (note < 0 || note > 127) return;
    gate_[note].store(false);
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream*, void* data, int32_t frames) {
    const int64_t t0 = monoNs();
    float* out = static_cast<float*>(data);
    const float sr = float(sr_);

    // TAP: щелчок начинается с первого отсчёта этого буфера.
    uint32_t seq = tapSeq_.load(std::memory_order_acquire);
    if (seq != tapSeen_) {
        tapSeen_ = seq;
        clickEnv_ = 1.0f; clickPhase_ = 0; clickFreq_ = 1500;
        tapTouchSeen_.store(tapTouchNs_.load());
        tapFrame_.store(frameCount_);
    }

    const bool toneOn = tone_.load(std::memory_order_relaxed);
    const float toneStep = kTwoPi * 440.0f / sr;
    const float rampK = 1.0f / (0.005f * sr);            // 5 мс
    const float clickDecay = std::exp(-1.0f / (0.004f * sr));
    const float attK = 1.0f / (0.002f * sr), relK = std::exp(-1.0f / (0.08f * sr));

    // Ноты MIDI: новые нажатия перезапускают атаку.
    for (int n = 0; n < 128; ++n) {
        uint32_t c = onCount_[n].load(std::memory_order_relaxed);
        if (c != noteSeen_[n]) { noteSeen_[n] = c; noteHeld_[n] = true; }
        if (!gate_[n].load(std::memory_order_relaxed)) noteHeld_[n] = false;
    }

    pendingClickFrames_ = -1;
    scheduleLinkClick(frames);

    for (int i = 0; i < frames; ++i) {
        float s = 0;
        float target = toneOn ? 0.2f : 0.0f;
        if (toneGain_ < target) toneGain_ = std::fmin(target, toneGain_ + rampK * 0.2f);
        else if (toneGain_ > target) toneGain_ = std::fmax(target, toneGain_ - rampK * 0.2f);
        if (toneGain_ > 0) {
            s += toneGain_ * std::sin(tonePhase_);
            tonePhase_ += toneStep;
            if (tonePhase_ > kTwoPi) tonePhase_ -= kTwoPi;
        }
        if (pendingClickFrames_ == i) {
            clickEnv_ = 1.0f; clickPhase_ = 0; clickFreq_ = pendingClickFreq_;
            pendingClickFrames_ = -1;
        }
        if (clickEnv_ > 1e-4f) {
            s += 0.5f * clickEnv_ * std::sin(clickPhase_);
            clickPhase_ += kTwoPi * clickFreq_ / sr;
            clickEnv_ *= clickDecay;
        }
        out[2 * i] = s;
        out[2 * i + 1] = s;
    }

    // Синусы нот (отдельным проходом по активным нотам).
    for (int n = 0; n < 128; ++n) {
        if (!noteHeld_[n] && noteEnv_[n] < 1e-4f) continue;
        const float amp = 0.15f * vel_[n].load(std::memory_order_relaxed);
        const float step = kTwoPi * 440.0f * std::pow(2.0f, (n - 69) / 12.0f) / sr;
        float env = noteEnv_[n], ph = notePhase_[n];
        for (int i = 0; i < frames; ++i) {
            if (noteHeld_[n]) env = std::fmin(1.0f, env + attK); else env *= relK;
            float v = amp * env * std::sin(ph);
            ph += step;
            if (ph > kTwoPi) ph -= kTwoPi;
            out[2 * i] += v;
            out[2 * i + 1] += v;
        }
        noteEnv_[n] = env; notePhase_[n] = ph;
    }

    frameCount_ += frames;
    lastFrames_.store(frames, std::memory_order_relaxed);

    // Загрузка колбэка: время расчёта / длительность буфера.
    const float load = float(double(monoNs() - t0) / (1e9 * frames / sr_));
    const float avg = loadAvg_.load(std::memory_order_relaxed);
    loadAvg_.store(avg + 0.02f * (load - avg), std::memory_order_relaxed);
    if (load > loadMaxHold_) loadMaxHold_ = load;
    if (++loadMaxTicks_ >= 400) {  // ~1–2 с
        loadMax_.store(loadMaxHold_, std::memory_order_relaxed);
        loadMaxHold_ = 0; loadMaxTicks_ = 0;
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::scheduleLinkClick(int32_t frames) {
    if (!link_ || !link_->enabled()) return;
    // Время Link первого кадра буфера (по счётчику кадров) + задержка выхода.
    const auto latency = std::chrono::microseconds(outLatencyUs_.load(std::memory_order_relaxed));
    const auto tStart = link_->hostTimeAtFrame(double(frameCount_)) + latency;
    if (!linkClick_.load(std::memory_order_relaxed)) { lastClickBeat_ = -1e9; return; }
    const auto bufDur = std::chrono::microseconds(int64_t(1e6 * frames / sr_));
    double b0 = 0, b1 = 0, phaseAtNext = 0;
    link_->beatsAt(tStart, tStart + bufDur, b0, b1, phaseAtNext);
    if (b0 < 0 || b1 <= b0) return;
    const double next = std::floor(b0) + 1.0;
    if (next > b1 || next <= lastClickBeat_) return;   // одна доля — один щелчок
    lastClickBeat_ = next;
    int64_t off = int64_t((next - b0) / (b1 - b0) * frames);
    if (off >= frames) off = frames - 1;
    pendingClickFrames_ = off;
    pendingClickFreq_ = (phaseAtNext < 0.5) ? 2000.0f : 1000.0f;  // первая доля такта — выше
}

void AudioEngine::info(double* o) {
    for (int i = 0; i < kInfoSize; ++i) o[i] = -1;
    std::lock_guard<std::mutex> g(lock_);
    if (!stream_) { o[0] = 0; return; }
    auto* s = stream_.get();
    o[0] = 1;                                         // поток открыт
    o[1] = s->getSampleRate();
    o[2] = s->getFramesPerBurst();
    o[3] = s->getBufferSizeInFrames();
    o[4] = s->getBufferCapacityInFrames();
    o[5] = lastFrames_.load();
    o[6] = s->getAudioApi() == oboe::AudioApi::AAudio ? 2 : 1;   // 2 AAudio, 1 OpenSL ES
    o[7] = s->getSharingMode() == oboe::SharingMode::Exclusive ? 1 : 0;
    o[8] = s->getPerformanceMode() == oboe::PerformanceMode::LowLatency ? 1 : 0;
    o[9] = oboe::OboeExtensions::isMMapUsed(s) ? 1 : 0;
    auto lat = s->calculateLatencyMillis();
    o[10] = lat ? lat.value() : -1;
    o[18] = lat ? 0 : double(int(lat.error()));      // код ошибки метки времени
    o[19] = double(int(s->getState()));
    if (lat) outLatencyUs_.store(int64_t(lat.value() * 1000.0));
    o[11] = loadAvg_.load() * 100.0;
    o[12] = loadMax_.load() * 100.0;
    auto xr = s->getXRunCount();
    o[13] = xr ? xr.value() : -1;
    // Касание -> звук: момент вывода кадра щелчка (по метке времени потока) минус время касания.
    const int64_t tf = tapFrame_.load();
    if (tf >= 0) {
        int64_t pos = 0, ns = 0;
        if (s->getTimestamp(CLOCK_MONOTONIC, &pos, &ns) == oboe::Result::OK) {
            const double presentNs = double(ns) + double(tf - pos) * 1e9 / s->getSampleRate();
            o[14] = (presentNs - double(tapTouchSeen_.load())) / 1e6;
        }
    }
    o[15] = s->getDeviceId();
    o[16] = s->getChannelCount();
    o[17] = s->getBufferSizeInFrames() * 1000.0 / s->getSampleRate();  // буфер, мс
}
