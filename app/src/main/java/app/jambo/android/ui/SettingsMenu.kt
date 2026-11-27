package app.jambo.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative.Act
import app.jambo.android.Native
import kotlinx.coroutines.delay

/** Вкладки SETTINGS — только те, чьи функции уже есть (порядок — как в Swift-версии); остальные — с функциями. */
private val TABS = listOf("RECORD", "RANDOM", "MORPH", "LEVEL", "APPEARANCE", "AUDIO ENGINE")

/** SETTINGS (SettingsMenu.swift): стрелка назад; подпись SECTIONS и вкладки по 3 в ряд; ниже, с чертой, — вкладка.
 *  Настройки — на устройстве (SharedPreferences), общие для проектов. */
@Composable
fun SettingsPage(s: ScreenState, back: () -> Unit) {
    Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        MenuBackHeader("SETTINGS", back)
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(end = 6.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            FieldLabel("SECTIONS")
            for (r in TABS.indices step 3) {
                val row = TABS.subList(r, minOf(r + 3, TABS.size))
                MenuChoices(row.mapIndexed { i, t -> t to (s.settingsTab == r + i) }) { s.settingsTab = r + it }
            }
            Divider()
            when (s.settingsTab) {
                0 -> RecordTab(s)
                1 -> RandomTab(s)
                2 -> MorphTab(s)
                3 -> LevelTab(s)
                4 -> AppearanceTab(s)
                else -> EngineTab()
            }
        }
    }
}

@Composable
private fun RecordTab(s: ScreenState) {
    val st = s.settings
    ToggleRow("QUANTIZE", st[0] != 0) { s.act(Act.SET_RECORD_SETTINGS, if (it) 1 else 0, st[1]) }
    MenuNote("ON: a note played with REC lands on the nearest step. OFF: on the step where it was played.")
}

@Composable
private fun RandomTab(s: ScreenState) {
    val options = s.settings[5]
    val names = listOf("CHORDS / ARP", "DRUMS", "SAMPLES", "AUTO SWING", "STEPS ALL TRACKS", "SHOW PART TYPE")
    names.forEachIndexed { k, title ->
        val bit = 1 shl k
        ToggleRow(title, options and bit != 0) { on ->
            s.act(Act.SET_RANDOM_SETTINGS, if (on) options or bit else options and bit.inv())
        }
    }
}

@Composable
private fun MorphTab(s: ScreenState) {
    val st = s.settings
    val hold = st[3] != 0
    FieldLabel("MODE")
    MenuChoices(listOf("TAP" to !hold, "HOLD" to hold)) { s.act(Act.SET_MORPH_SETTINGS, st[2], it) }
    MenuNote(if (hold) "HOLD: while you hold MORPH, knobs change the selected track; let go — MORPH turns off and the knobs return."
        else "TAP: tap MORPH to turn it on — knobs change the selected track; tap again to turn it off and return the knobs.")
    MenuNote("Hold SHIFT + MORPH (in either mode): knobs change every track that has the parameter (each track moves by the same amount from its own value); let go — MORPH turns off.")
    FieldLabel("RETURN")
    MenuChoices(s.morphReturnNames.mapIndexed { k, n -> n to (st[2] == k) }) { s.act(Act.SET_MORPH_SETTINGS, it, st[3]) }
    MenuNote("How fast the knobs glide back when MORPH is turned off: OFF — at once, or over 1/4 bar to 2 bars at the current tempo.")
}

@Composable
private fun LevelTab(s: ScreenState) {
    val rel = s.settings[4] != 0
    FieldLabel("TRACK LEVEL FADER")
    MenuChoices(listOf("ABSOLUTE" to !rel, "RELATIVE" to rel)) { s.act(Act.SET_LEVEL_RELATIVE, it) }
    MenuNote("ABSOLUTE: the fader on the right is the track volume in MIX. RELATIVE: the fader trims the track down from its MIX volume — it cannot go above the MIX volume (for example −10 dB), down to silence; when the MIX volume changes, the trimmed level moves with it. The trim is kept with the track and only works in RELATIVE.")
}

@Composable
private fun AppearanceTab(s: ScreenState) {
    FieldLabel("THEME")
    MenuChoices(listOf("SYSTEM", "LIGHT", "DARK").mapIndexed { i, t -> t to (s.theme == i) }) { s.theme = it }
    MenuNote("SYSTEM follows the tablet setting.")
    FieldLabel("MASTER FX DISPLAY SCALE")
    val scales = listOf(48, 32, 16)
    MenuChoices(scales.map { "$it dB" to (s.displayScale == it) }) { s.displayScale = scales[it] }
    MenuNote("Zoom of the level displays in the master effect windows (COMP, CLIP, LIM): from 0 dBFS down to −48, −32 or −16 dB.")
}

/** AUDIO ENGINE: фактическая частота, буфер и задержка (строка «Now:», раз в 0,5 с). */
@Composable
private fun EngineTab() {
    var line by remember { mutableStateOf("") }
    LaunchedEffect(Unit) {
        while (true) {
            val a = Native.audioInfo()
            line = if (a[0] <= 0) "audio off" else buildString {
                append("${(a[1] / 1000).let { if (it == Math.rint(it)) it.toInt().toString() else "%.1f".format(it) }} kHz")
                append(" · buffer ${a[3].toInt()} (burst ${a[2].toInt()})")
                if (a[10] > 0) append(" · latency ${"%.1f".format(a[10])} ms")
                append(if (a[6] == 2.0) " · AAudio" else " · OpenSL ES")
                if (a[7] == 1.0) append(" exclusive")
            }
            delay(500)
        }
    }
    MenuNote("Now: $line")
    MenuNote("48 kHz, low latency (AAudio); Android may choose another buffer.")
}
