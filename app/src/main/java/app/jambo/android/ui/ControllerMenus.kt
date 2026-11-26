package app.jambo.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import app.jambo.android.BleMidiPeripheral
import app.jambo.android.MidiBinding
import app.jambo.android.MidiHub
import app.jambo.android.Native
import app.jambo.android.midiTargetLabel
import kotlinx.coroutines.delay

data class ControllerActions(
    val ble: BleMidiPeripheral, val scan: () -> Unit,
    val advertise: (Boolean) -> Unit, val link: (Boolean) -> Unit,
)

@Composable
fun MidiMenuPage(midi: MidiHub, actions: ControllerActions, back: () -> Unit, close: () -> Unit) {
    val c = midi.controls
    var page by remember { mutableStateOf("MIDI") }
    var chosen by remember { mutableStateOf<String?>(null) }
    var clear by remember { mutableStateOf(false) }
    val peripheral = actions.ble
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        MenuBackHeader(chosen ?: page) {
            when {
                chosen != null -> chosen = null
                page != "MIDI" -> page = "MIDI"
                else -> back()
            }
        }
        androidx.compose.runtime.key(page, chosen) {
            Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(end = 6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                val device = chosen
                when {
                    device != null -> {
                        ToggleRow("DEVICE ON", device !in c.disabled) {
                            c.setDevice(device, it)
                            if (!it) midi.sync.disconnected(device)
                        }
                        val maps = c.bindings.filter { it.source.device == device }
                        MenuHead("MAPPINGS (${maps.size})")
                        if (maps.isEmpty()) MenuNote("Use MIDI LEARN to assign controls on this device.")
                        for (mapping in maps) {
                            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                MenuItem("${midiTargetLabel(mapping.target)} · ${mapping.inputLabel}", Modifier.weight(1f)) {
                                    close()
                                    c.edit(mapping)
                                }
                                MenuItem("✕", Modifier.width(44.dp)) { c.remove(mapping) }
                            }
                        }
                        if (maps.isNotEmpty()) MenuItem("CLEAR ALL", red = true) { clear = true }
                    }
                    page == "DEVICES" -> {
                        if (c.knownDevices.isEmpty()) MenuNote("No MIDI devices connected.")
                        for (key in c.knownDevices.sorted()) {
                            val row = midi.devices.firstOrNull { it.key == key }
                            val connected = row?.open == true || midi.inputs.any { it.device == key }
                            val status = if (connected) "Connected" else "Not connected"
                            val count = c.bindings.count { it.source.device == key }
                            MenuItem("${row?.name ?: key} · $status · $count") { chosen = key }
                        }
                    }
                    page == "BLUETOOTH MIDI" -> {
                        MenuItem(if (midi.scanning.value) "SCANNING…" else "SCAN BLUETOOTH", enabled = !midi.scanning.value) { actions.scan() }
                        for (device in midi.ble) MenuItem(device.name) { midi.connectBle(device.address) }
                        ToggleRow("ADVERTISE", peripheral.advertising.value || peripheral.connected.value != null, actions.advertise)
                        peripheral.connected.value?.let { MenuNote("Connected · $it") }
                    }
                    page == "MIDI MONITOR" -> {
                        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            MenuItem("CLEAR", Modifier.weight(1f)) { midi.log.clear() }
                            Text("Clock ticks: ${midi.clockTicks.value}", color = LocalTheme.current.dim, fontSize = 11.tsp())
                        }
                        for (line in midi.log) MenuNote(line)
                    }
                    else -> {
                        MenuItem("DEVICES") { page = "DEVICES" }
                        Divider()
                        MenuItem("MIDI LEARN") {
                            c.setLearn(true)
                            close()
                        }
                        MenuNote("Tap a knob or button, then move a MIDI control. Knobs follow the current page.")
                        FieldLabel("KNOB PRESS")
                        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            MenuItem("TOUCH", Modifier.weight(1f), current = c.pressTouch) { c.setPress(true) }
                            MenuItem("MIDI MAP", Modifier.weight(1f), current = !c.pressTouch) { c.setPress(false) }
                        }
                        Divider()
                        MenuItem("BLUETOOTH MIDI") { page = "BLUETOOTH MIDI" }
                        MenuItem("MIDI MONITOR") { page = "MIDI MONITOR" }
                    }
                }
            }
        }
    }
    if (clear) ChoiceDialog("Clear all mappings?", chosen, listOf(
        Triple("CLEAR", true) { chosen?.let(c::clear); clear = false },
        Triple("CANCEL", true) { clear = false },
    )) { clear = false }
}

@Composable
fun SyncMenuPage(midi: MidiHub, actions: ControllerActions, back: () -> Unit) {
    val sync = midi.sync
    val s = sync.settings
    var link by remember { mutableStateOf(Native.linkInfo(0)) }
    LaunchedEffect(Unit) {
        while (true) {
            link = Native.linkInfo(0)
            delay(250)
        }
    }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        MenuBackHeader("SYNC", back)
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(end = 6.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)) {
            MenuHead("GENERAL")
            FieldLabel("SYNC QUANTUM")
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (q in listOf(1, 2, 4, 8)) MenuItem(if (q == 1) "1 BAR" else "$q BARS", Modifier.weight(1f),
                    current = s.quantum == q) { sync.change { it.copy(quantum = q) } }
            }
            Divider()
            MenuHead("MIDI CLOCK")
            ToggleRow("MIDI CLOCK SYNC", s.clock) { on -> sync.change { it.copy(clock = on) } }
            ToggleRow("SEND CLOCK", s.send) { on -> sync.change { it.copy(send = on) } }
            if (s.send) {
                FieldLabel("CLOCK OUTPUTS")
                MenuItem("ALL OUTPUTS", current = s.clockOuts.isEmpty()) { sync.change { it.copy(clockOuts = emptySet()) } }
                for (port in midi.outputs) MenuItem(port.label, current = port.key in s.clockOuts) {
                    sync.change { it.copy(clockOuts = if (port.key in it.clockOuts) it.clockOuts - port.key else it.clockOuts + port.key) }
                }
                if (midi.outputs.isEmpty()) MenuNote("No MIDI outputs connected.")
            }
            ToggleRow("RECEIVE CLOCK", s.receive) { on -> sync.change { it.copy(receive = on) } }
            if (s.receive) {
                FieldLabel("CLOCK INPUT")
                MenuItem("ALL INPUTS", current = s.clockIn == null) { sync.change { it.copy(clockIn = null) } }
                for (port in midi.inputs) MenuItem(port.label, current = s.clockIn == port.key) {
                    sync.change { it.copy(clockIn = port.key) }
                }
                if (midi.inputs.isEmpty()) MenuNote("No MIDI inputs connected.")
            }
            ToggleRow("SYNC START/STOP", s.startStop) { on -> sync.change { it.copy(startStop = on) } }
            FieldLabel("MIDI OFFSET")
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem("◀", Modifier.width(48.dp)) { sync.change { it.copy(offset = (it.offset - 1).coerceAtLeast(-200)) } }
                MenuItem("${s.offset} ms", Modifier.weight(1f), enabled = s.offset != 0) { sync.change { it.copy(offset = 0) } }
                MenuItem("▶", Modifier.width(48.dp)) { sync.change { it.copy(offset = (it.offset + 1).coerceAtMost(200)) } }
            }
            Divider()
            MenuHead("ABLETON LINK")
            ToggleRow("ABLETON LINK", link[0] != 0.0) {
                actions.link(it)
                link = Native.linkInfo(0)
            }
            if (link[0] != 0.0) {
                val beats = 4 * s.quantum
                val phase = ((link[3] % beats + beats) % beats).toInt() + 1
                MenuNote("${link[1].toInt()} peers · Beat $phase / $beats")
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    MenuItem("−", Modifier.width(48.dp)) { Native.linkSetTempo((link[2] - 1).coerceAtLeast(20.0)) }
                    Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
                        MenuNote("${"%.1f".format(link[2])} BPM")
                    }
                    MenuItem("+", Modifier.width(48.dp)) { Native.linkSetTempo((link[2] + 1).coerceAtMost(999.0)) }
                }
            }
        }
    }
}

@Composable
fun MidiLearnStrip(s: ScreenState, modifier: Modifier) {
    val midi = s.midi
    val c = midi.controls
    val target = c.learnTarget
    val known = c.bindings.lastOrNull { it.target == target }
    Row(modifier.fillMaxWidth(0.72f).menuBox().padding(8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text(if (target < 0) "MIDI LEARN · Tap a knob or button" else "${midiTargetLabel(target)} · Move a MIDI control",
            Modifier.weight(1f), color = LocalTheme.current.text, fontSize = 12.tsp())
        MenuItem("PRESS", Modifier.width(84.dp), current = c.learnPress) { c.learnPress = !c.learnPress }
        MenuItem("MIDI MAP", Modifier.width(110.dp), enabled = target >= 0 && (known != null || midi.inputs.isNotEmpty())) {
            (known ?: midi.inputs.firstOrNull()?.let { MidiBinding(it, 0xB0, 0, target) })?.let(c::edit)
        }
        MenuItem("DONE", Modifier.width(84.dp)) { c.setLearn(false) }
    }
}

@Composable
fun MidiMapEditor(midi: MidiHub) {
    val c = midi.controls
    val m = c.editor ?: return
    fun update(next: MidiBinding) { c.editor = next }
    var selectAction by remember { mutableStateOf(false) }
    val encoderNames = listOf("ABSOLUTE", "OFFSET 64", "2’S COMPLEMENT", "SIGNED BIT")
    Dialog(onDismissRequest = c::closeEditor) {
        Column(Modifier.width(410.dp).menuBox().padding(12.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(6.dp)) {
            MenuHead("MIDI MAP · ${midiTargetLabel(m.target)}")
            MenuNote(m.source.label)
            if (midi.inputs.size > 1) {
                FieldLabel("INPUT PORT")
                for (port in midi.inputs) MenuItem(port.label, current = m.source.key == port.key) {
                    update(m.copy(source = port))
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem("CC", Modifier.weight(1f), current = m.status and 0xF0 == 0xB0) {
                    update(m.copy(status = 0xB0 or (m.status and 15)))
                }
                MenuItem("NOTE", Modifier.weight(1f), current = m.status and 0xF0 == 0x90) {
                    update(m.copy(status = 0x90 or (m.status and 15)))
                }
            }
            MapNumber("CHANNEL", (m.status and 15) + 1, 1..16) { update(m.copy(status = (m.status and 0xF0) or (it - 1))) }
            MapNumber("NUMBER", m.number, 0..127) { update(m.copy(number = it)) }
            if (m.target < 16) {
                MenuItem(encoderNames[m.encoder.coerceIn(0, 3)]) { update(m.copy(encoder = (m.encoder + 1) % 4)) }
                MapNumber("MIN %", m.min, 0..100) { update(m.copy(min = it)) }
                MapNumber("MAX %", m.max, 0..100) { update(m.copy(max = it)) }
            } else {
                if (m.target in 16..31 && !c.pressTouch) {
                    FieldLabel("ACTION")
                    MenuItem(if (m.action < 0) "RESET KNOB" else midiTargetLabel(m.action)) { selectAction = !selectAction }
                    if (selectAction) {
                        MenuItem("RESET KNOB", current = m.action < 0) { update(m.copy(action = -1)); selectAction = false }
                        for (target in 32..41) MenuItem(midiTargetLabel(target), current = m.action == target) {
                            update(m.copy(action = target))
                            selectAction = false
                        }
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    MenuItem("MOMENTARY", Modifier.weight(1f), current = m.mode == 0) { update(m.copy(mode = 0)) }
                    MenuItem("TOGGLE", Modifier.weight(1f), current = m.mode == 1) { update(m.copy(mode = 1)) }
                    MenuItem("LATCH", Modifier.weight(1f), current = m.mode == 2) { update(m.copy(mode = 2)) }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem("UNMAP", Modifier.weight(1f), enabled = c.editorOriginal != null) {
                    c.editorOriginal?.let(c::remove)
                    c.closeEditor()
                }
                MenuItem("CANCEL", Modifier.weight(1f)) { c.closeEditor() }
                MenuItem("SAVE", Modifier.weight(1f)) {
                    c.save(m, c.editorOriginal ?: m)
                    c.closeEditor()
                }
            }
        }
    }
}

@Composable
private fun MapNumber(label: String, value: Int, range: IntRange, change: (Int) -> Unit) {
    var editing by remember { mutableStateOf(false) }
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f)) { FieldLabel(label) }
        MenuItem("−", Modifier.width(42.dp), enabled = value > range.first) { change(value - 1) }
        MenuItem(value.toString(), Modifier.width(86.dp)) { editing = true }
        MenuItem("+", Modifier.width(42.dp), enabled = value < range.last) { change(value + 1) }
    }
    if (editing) InputDialog(label, "$value", 3, { editing = false }) { text ->
        text.toIntOrNull()?.let { change(it.coerceIn(range)) }
        editing = false
    }
}
