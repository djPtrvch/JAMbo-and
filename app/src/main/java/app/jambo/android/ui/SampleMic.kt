package app.jambo.android.ui

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import kotlin.math.abs

/** Вход Android: моно PCM 48 кГц, до 60 секунд. */
internal class SampleMic {
    @Volatile private var stopRequested = false
    @Volatile private var blocks = 0
    private val envelope = FloatArray(maxFrames / blockSize)
    @Volatile var running = false
        private set

    fun prepare() {
        stopRequested = false
        blocks = 0
        running = true
    }

    fun stop() { stopRequested = true }

    fun peaks(): FloatArray {
        val count = blocks
        return FloatArray(640) { col ->
            val step = maxOf(1f, count / 640f, 1875 / 640f)
            val from = (col * step).toInt()
            val to = minOf(count, ((col + 1) * step).toInt())
            var peak = 0f
            for (i in from until to) peak = maxOf(peak, envelope[i])
            peak
        }
    }

    @SuppressLint("MissingPermission")
    suspend fun record(finish: (FloatArray?, Int) -> Unit): Boolean = withContext(NonCancellable + Dispatchers.IO) {
        var recorder: AudioRecord? = null
        var pcm: FloatArray? = null
        var written = 0
        try {
            val channel = AudioFormat.CHANNEL_IN_MONO
            val format = AudioFormat.ENCODING_PCM_FLOAT
            if (stopRequested) return@withContext true
            val buffer = AudioRecord.getMinBufferSize(rate, channel, format)
            if (buffer <= 0) return@withContext false
            val input = AudioRecord.Builder()
                .setAudioSource(MediaRecorder.AudioSource.MIC)
                .setAudioFormat(AudioFormat.Builder().setEncoding(format).setSampleRate(rate).setChannelMask(channel).build())
                .setBufferSizeInBytes(maxOf(buffer, 4096))
                .build()
            recorder = input
            if (input.state != AudioRecord.STATE_INITIALIZED) return@withContext false
            val data = FloatArray(maxFrames)
            pcm = data
            input.startRecording()
            while (!stopRequested && written < data.size) {
                val n = input.read(data, written, minOf(1024, data.size - written), AudioRecord.READ_BLOCKING)
                if (n < 0) return@withContext false
                written += n
                while (blocks < written / blockSize) {
                    val pos = blocks * blockSize
                    var peak = 0f
                    for (i in pos until pos + blockSize) peak = maxOf(peak, abs(data[i]))
                    envelope[blocks] = peak
                    blocks++
                }
            }
            true
        } catch (_: IllegalStateException) {
            false
        } catch (_: SecurityException) {
            false
        } catch (_: IllegalArgumentException) {
            false
        } finally {
            recorder?.let {
                if (it.recordingState == AudioRecord.RECORDSTATE_RECORDING) runCatching { it.stop() }
                it.release()
            }
            running = false
            finish(pcm, written)
        }
    }

    private companion object {
        const val rate = 48000
        const val maxFrames = 60 * rate
        const val blockSize = 128
    }
}
