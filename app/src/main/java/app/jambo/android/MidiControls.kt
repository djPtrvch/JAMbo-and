package app.jambo.android

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.json.JSONArray
import org.json.JSONObject

data class MidiEndpoint(val device: String, val port: Int, val label: String) {
    val key get() = "$device:$port"
}

data class MidiBinding(
    val source: MidiEndpoint,
    val status: Int,
    val number: Int,
    val target: Int,
    val encoder: Int = 0,
    val min: Int = 0,
    val max: Int = 100,
    val mode: Int = 0,
    val action: Int = -1,
) {
    val inputLabel get() = "${if (status and 0xF0 == 0xB0) "CC" else "NOTE"} $number · CH ${(status and 15) + 1}"
}

fun midiTargetLabel(target: Int): String = when (target) {
    in 0..15 -> "KNOB ${target / 4 + 1}.${target % 4 + 1}"
    in 16..31 -> midiTargetLabel(target - 16) + " PRESS"
    in 32..41 -> listOf("PLAY", "REC", "UNDO", "REDO", "METRONOME", "SHIFT", "FX", "MOD", "EQ", "PATTERNS")[target - 32]
    in 48..63 -> "STEP ${target - 47}"
    else -> ""
}

class MidiControls(ctx: Context) {
    private val prefs = ctx.getSharedPreferences("jambo-midi", Context.MODE_PRIVATE)
    val bindings = mutableStateListOf<MidiBinding>()
    val knownDevices = mutableStateListOf<String>()
    var disabled by mutableStateOf(prefs.getStringSet("disabled", emptySet())!!.toSet())
        private set
    var pressTouch by mutableStateOf(prefs.getBoolean("pressTouch", true))
        private set
    var learning by mutableStateOf(false)
        private set
    var learnTarget by mutableIntStateOf(-1)
        private set
    var learnPress by mutableStateOf(false)
    var editor by mutableStateOf<MidiBinding?>(null)
    private data class Source(val endpoint: MidiEndpoint, val status: Int, val number: Int)
    private data class Held(val id: Int, val binding: MidiBinding?, val touched: Boolean = false)
    private val held = mutableMapOf<Source, Held>()
    private val toggled = mutableMapOf<Source, MidiBinding>()
    private var nextId = 2_000_000

    init {
        knownDevices.addAll(prefs.getStringSet("knownDevices", emptySet())!!.sorted())
        val data = runCatching { JSONArray(prefs.getString("mappings", "[]")) }.getOrElse { JSONArray() }
        for (i in 0 until data.length()) {
            runCatching {
                val o = data.getJSONObject(i)
                val src = MidiEndpoint(o.getString("device"), o.getInt("port"), o.getString("label"))
                bindings += MidiBinding(src, o.getInt("status"), o.getInt("number"), o.getInt("target"),
                    o.optInt("encoder"), o.optInt("min"), o.optInt("max", 100), o.optInt("mode"), o.optInt("action", -1))
                rememberDevice(src.device)
            }
        }
    }

    fun rememberDevice(device: String) {
        if (device in knownDevices) return
        knownDevices.add(device)
        prefs.edit().putStringSet("knownDevices", knownDevices.toSet()).apply()
    }

    fun setDevice(device: String, on: Boolean) {
        if (!on) releaseDevice(device)
        disabled = if (on) disabled - device else disabled + device
        prefs.edit().putStringSet("disabled", disabled).apply()
    }

    fun setPress(touch: Boolean) {
        releaseDevice()
        pressTouch = touch
        prefs.edit().putBoolean("pressTouch", touch).apply()
    }

    fun setLearn(on: Boolean) {
        releaseDevice()
        learning = on
        closeEditor()
        learnPress = false
    }

    fun pick(target: Int) {
        if (!learning) return
        if (target == learnTarget) bindings.lastOrNull { it.target == target }?.let { edit(it) }
        learnTarget = target
    }

    var editorOriginal by mutableStateOf<MidiBinding?>(null)
        private set

    fun edit(binding: MidiBinding) {
        editorOriginal = binding.takeIf { it in bindings }
        editor = binding
        learnTarget = binding.target
    }

    fun closeEditor() {
        editor = null
        editorOriginal = null
        learnTarget = -1
    }

    fun save(binding: MidiBinding, previous: MidiBinding = binding) {
        releaseDevice(previous.source.device)
        bindings.remove(previous)
        bindings.removeAll { it.source.key == binding.source.key && it.status == binding.status && it.number == binding.number }
        bindings.add(binding)
        persist()
    }

    fun remove(binding: MidiBinding) {
        releaseDevice(binding.source.device)
        bindings.remove(binding)
        persist()
    }

    fun clear(device: String) {
        releaseDevice(device)
        bindings.removeAll { it.source.device == device }
        persist()
    }

    fun releaseDevice(device: String? = null) {
        held.entries.removeAll { (src, active) ->
            (device == null || src.endpoint.device == device).also { if (it) release(src, active) }
        }
        toggled.entries.removeAll { (src, binding) ->
            (device == null || src.endpoint.device == device).also {
                if (it) invoke(binding, false)
            }
        }
    }

    private fun release(source: Source, active: Held) {
        if (active.binding == null) AppNative.midiNoteFrom(source.number, 0, active.id)
        else if (active.touched) invoke(active.binding, false)
    }

    private fun invoke(binding: MidiBinding, down: Boolean) {
        val target = if (!pressTouch && binding.target in 16..31 && binding.action >= 32) binding.action else binding.target
        AppNative.midiButton(target, down, pressTouch, binding.mode != 0)
    }

    fun message(endpoint: MidiEndpoint, status: Int, number: Int, value: Int) {
        if (endpoint.device in disabled) return
        val type = status and 0xF0
        if (type != 0x80 && type != 0x90 && type != 0xB0) return
        val note = type != 0xB0
        val down = type != 0x80 && value > 0
        val normalized = (status and 15) or if (note) 0x90 else 0xB0
        val source = Source(endpoint, normalized, number)
        if (note && !down && !held.containsKey(source)) return
        if ((learning || editor != null) && learnTarget >= 0 && (down || !note)) {
            val current = editor?.takeIf { it.target == learnTarget }
                ?: bindings.lastOrNull { it.target == learnTarget && it.source.device == endpoint.device }
            val next = MidiBinding(endpoint, normalized, number, learnTarget, current?.encoder ?: 0,
                current?.min ?: 0, current?.max ?: 100, current?.mode ?: 0, current?.action ?: -1)
            if (editor != null) editor = next
            else {
                save(next, current ?: next)
                learnTarget = -1
            }
            return
        }
        if (note && !down) {
            held.remove(source)?.let { release(source, it) }
            return
        }
        val mapping = bindings.lastOrNull {
            it.source.key == endpoint.key && it.status == normalized && it.number == number
        }
        if (mapping == null) {
            if (note) {
                val active = held.getOrPut(source) { Held(nextId++, null) }
                AppNative.midiNoteFrom(number, value, active.id)
            }
        } else if (mapping.target < 16) {
            AppNative.midiControl(mapping.target, value, mapping.encoder, mapping.min / 100.0, mapping.max / 100.0)
        } else if ((if (mapping.mode == 2 && !note) value >= 64 else down) && source !in held) {
            if (mapping.mode == 1) {
                val on = toggled.remove(source) == null
                if (on) toggled[source] = mapping
                invoke(mapping, on)
                held[source] = Held(0, mapping)
            } else {
                held[source] = Held(0, mapping, true)
                invoke(mapping, true)
            }
        } else if (if (mapping.mode == 2 && !note) value < 64 else !down) {
            held.remove(source)?.let { release(source, it) }
        }
    }

    private fun persist() {
        val data = JSONArray()
        for (m in bindings) data.put(JSONObject().put("device", m.source.device).put("port", m.source.port)
            .put("label", m.source.label).put("status", m.status).put("number", m.number).put("target", m.target)
            .put("encoder", m.encoder).put("min", m.min).put("max", m.max).put("mode", m.mode).put("action", m.action))
        prefs.edit().putString("mappings", data.toString()).apply()
    }
}
