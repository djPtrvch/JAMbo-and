package app.jambo.android

import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

data class MidiSyncSettings(
    val clock: Boolean = false,
    val send: Boolean = true,
    val receive: Boolean = false,
    val startStop: Boolean = true,
    val offset: Int = 0,
    val clockIn: String? = null,
    val clockOuts: Set<String> = emptySet(),
    val quantum: Int = 1,
)

/** Планировщик C++ получает часы Android, выходы принимают пакеты с метками времени. */
class MidiSync(ctx: Context, private val hub: MidiHub) {
    private val prefs = ctx.getSharedPreferences("jambo-sync", Context.MODE_PRIVATE)
    private val main = Handler(Looper.getMainLooper())
    private var started = false
    private var activeSource: MidiEndpoint? = null
    private var last = 0.0
    var settings by mutableStateOf(MidiSyncSettings(
        prefs.getBoolean("clock", false), prefs.getBoolean("send", true),
        prefs.getBoolean("receive", false), prefs.getBoolean("startStop", true),
        prefs.getInt("offset", 0).coerceIn(-200, 200), prefs.getString("clockIn", null),
        prefs.getStringSet("clockOuts", emptySet())!!.toSet(), prefs.getInt("quantum", 1),
    ))
        private set

    private val pump = object : Runnable {
        override fun run() {
            if (!started) return
            val now = System.nanoTime() / 1e9
            if (activeSource != null && now - last > 0.5) clearSource()
            val s = settings
            if (s.clock) AppNative.midiClockBatch(now, s.send, s.receive, s.startStop, s.offset)?.let {
                hub.sendClock(it, s.clockOuts, activeSource?.device)
            }
            main.postDelayed(this, if (s.clock) 12 else 100)
        }
    }

    fun change(update: (MidiSyncSettings) -> MidiSyncSettings) {
        val before = settings
        val next = update(before)
        if (before == next) return
        if (before.clockIn != next.clockIn || !next.clock || !next.receive) clearSource()
        if (before.clockOuts != next.clockOuts || before.send != next.send || before.offset != next.offset || before.clock != next.clock)
            clearOutput()
        settings = next
        prefs.edit().putBoolean("clock", next.clock).putBoolean("send", next.send)
            .putBoolean("receive", next.receive).putBoolean("startStop", next.startStop)
            .putInt("offset", next.offset).putString("clockIn", next.clockIn)
            .putStringSet("clockOuts", next.clockOuts).putInt("quantum", next.quantum).apply()
        if (started) {
            main.removeCallbacks(pump)
            main.post(pump)
        }
    }

    fun start() {
        if (started) return
        started = true
        main.post(pump)
    }

    fun stop() {
        started = false
        main.removeCallbacks(pump)
        clearOutput()
        clearSource()
    }

    fun disconnected(device: String) {
        if (activeSource?.device == device) clearSource()
    }

    internal fun realtime(source: MidiEndpoint, status: Int, timestamp: Long) {
        val s = settings
        if (!started || !s.clock || !s.receive || s.clockIn != null && s.clockIn != source.key) return
        if (source.device in hub.controls.disabled || status != 0xF8 && !s.startStop) return
        val now = System.nanoTime() / 1e9
        val time = (timestamp / 1e9).coerceIn(now - 0.5, now + 0.25)
        if (activeSource != null && now - last > 0.5) clearSource()
        if (activeSource != null && activeSource?.key != source.key) return
        if (activeSource == null && status == 0xFC) return
        if (activeSource == null && s.send) clearOutput()
        activeSource = source
        last = now
        AppNative.midiRealtime(status, time, s.startStop, s.offset)
    }

    private fun clearOutput() {
        AppNative.midiClockBatch(System.nanoTime() / 1e9, false, settings.clock && settings.receive,
            settings.startStop, settings.offset)?.let { hub.sendClock(it, emptySet(), null) }
    }

    private fun clearSource() {
        activeSource = null
        last = 0.0
        AppNative.midiResetClock()
    }
}
