package app.jambo.android

import android.media.MediaCodec
import android.media.MediaDataSource
import android.media.MediaExtractor
import android.media.MediaFormat
import java.nio.ByteOrder

/**
 * Декодирование аудио и звука видео (MP3, M4A, AAC, FLAC, OGG, MP4…) через MediaExtractor и MediaCodec — отличие
 * Android (на iPad — AVAudioFile и AVAssetExportSession). WAV, AIFF и CAF читает C++ (SampleStore). Результат — PCM
 * float с чередованием каналов (не больше двух), частота файла; к 48 кГц пересчитывает C++.
 */
object MediaDecode {
    class Pcm(val data: FloatArray, val channels: Int, val rate: Int)

    private class Bytes(val b: ByteArray) : MediaDataSource() {
        override fun readAt(position: Long, buffer: ByteArray, offset: Int, size: Int): Int {
            if (position >= b.size) return -1
            val n = minOf(size.toLong(), b.size - position).toInt()
            System.arraycopy(b, position.toInt(), buffer, offset, n)
            return n
        }
        override fun getSize(): Long = b.size.toLong()
        override fun close() {}
    }

    /** Без звуковой дорожки или ошибка — null. */
    fun decode(bytes: ByteArray): Pcm? {
        val ex = MediaExtractor()
        try {
            ex.setDataSource(Bytes(bytes))
            val track = (0 until ex.trackCount).firstOrNull {
                ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: return null
            ex.selectTrack(track)
            val fmt = ex.getTrackFormat(track)
            val codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME)!!)
            fmt.setInteger(MediaFormat.KEY_PCM_ENCODING, android.media.AudioFormat.ENCODING_PCM_FLOAT)
            codec.configure(fmt, null, null, 0)
            codec.start()
            try {
                return run(ex, codec, fmt)
            } finally {
                codec.stop()
                codec.release()
            }
        } catch (_: Exception) {
            return null
        } finally {
            ex.release()
        }
    }

    private fun run(ex: MediaExtractor, codec: MediaCodec, inFmt: MediaFormat): Pcm? {
        var channels = inFmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        var rate = inFmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        var float = false
        var out = FloatArray(1 shl 16)
        var n = 0
        val info = MediaCodec.BufferInfo()
        var inDone = false
        while (true) {
            if (!inDone) {
                val i = codec.dequeueInputBuffer(10_000)
                if (i >= 0) {
                    val buf = codec.getInputBuffer(i)!!
                    val size = ex.readSampleData(buf, 0)
                    if (size < 0) {
                        codec.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        inDone = true
                    } else {
                        codec.queueInputBuffer(i, 0, size, ex.sampleTime, 0)
                        ex.advance()
                    }
                }
            }
            val o = codec.dequeueOutputBuffer(info, 10_000)
            if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                val f = codec.outputFormat
                channels = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                rate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                float = f.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                    f.getInteger(MediaFormat.KEY_PCM_ENCODING) == android.media.AudioFormat.ENCODING_PCM_FLOAT
            } else if (o >= 0) {
                val buf = codec.getOutputBuffer(o)!!.order(ByteOrder.nativeOrder())
                buf.position(info.offset)
                buf.limit(info.offset + info.size)
                val ch = maxOf(1, channels)
                val keep = minOf(2, ch)
                val frames = if (float) info.size / 4 / ch else info.size / 2 / ch
                if (n + frames * keep > out.size) out = out.copyOf(maxOf(out.size * 2, n + frames * keep))
                if (float) {
                    val fb = buf.asFloatBuffer()
                    for (k in 0 until frames) for (c in 0 until keep) out[n++] = fb.get(k * ch + c)
                } else {
                    val sb = buf.asShortBuffer()
                    for (k in 0 until frames) for (c in 0 until keep) out[n++] = sb.get(k * ch + c) / 32768f
                }
                codec.releaseOutputBuffer(o, false)
                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) break
            }
        }
        val keep = minOf(2, maxOf(1, channels))
        if (n == 0) return null
        return Pcm(out.copyOf(n), keep, rate)
    }
}
