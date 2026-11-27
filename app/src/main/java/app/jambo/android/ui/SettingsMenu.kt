package app.jambo.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
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
private enum class SettingsSection(val title: String) {
    RECORD("RECORD"), SAMPLER("SAMPLER"), ARRANGE("ARRANGE"), MORPH("MORPH"), RANDOM("RANDOM"),
    LEVEL("LEVEL"), DUCK("DUCK"), APPEARANCE("APPEARANCE"), ENGINE("AUDIO ENGINE")
}

/** SETTINGS (SettingsMenu.swift): стрелка назад; подпись SECTIONS и вкладки по 3 в ряд; ниже, с чертой, — вкладка.
 *  Настройки — на устройстве (SharedPreferences), общие для проектов. */
@Composable
fun SettingsPage(s: ScreenState, back: () -> Unit) {
    Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        MenuBackHeader("SETTINGS", back)
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(end = 6.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            FieldLabel("SECTIONS")
            SettingsSection.entries.chunked(3).forEach { row ->
                MenuChoices(row.map { it.title to (s.settingsTab == it.ordinal) }) { s.settingsTab = row[it].ordinal }
            }
            Divider()
            when (SettingsSection.entries.getOrElse(s.settingsTab) { SettingsSection.RECORD }) {
                SettingsSection.RECORD -> RecordTab(s)
                SettingsSection.SAMPLER -> SamplerTab(s)
                SettingsSection.ARRANGE -> ArrangeTab(s)
                SettingsSection.MORPH -> MorphTab(s)
                SettingsSection.RANDOM -> RandomTab(s)
                SettingsSection.LEVEL -> LevelTab(s)
                SettingsSection.DUCK -> DuckTab(s)
                SettingsSection.APPEARANCE -> AppearanceTab(s)
                SettingsSection.ENGINE -> EngineTab()
            }
        }
    }
}

@Composable
private fun RecordTab(s: ScreenState) {
    val st = s.settings
    ToggleRow("QUANTIZE", st[0] != 0) { s.act(Act.SET_RECORD_SETTINGS, if (it) 1 else 0, st[1]) }
    MenuNote("ON: a note played with REC lands on the nearest step. OFF: on the step where it was played.")
    val p = s.arrangePreferences
    FieldLabel("ARRANGE REC START")
    MenuChoices(listOf("PLAYHEAD" to !p.recordFromStart, "BEGINNING" to p.recordFromStart)) {
        s.arrangePreferences = p.copy(recordFromStart = it == 1)
    }
    MenuNote("When recording PATTERNS into the arrangement, start at the playhead or at the beginning.")
    ToggleRow("DRUM STEP PAINT", p.drumPaint) { s.arrangePreferences = p.copy(drumPaint = it) }
    MenuNote("On drum and sample tracks, dragging along the step row triggers each step you pass.")
    FieldLabel("METRONOME · ${p.clickLevel}%")
    val th = LocalTheme.current
    Slider(value = p.clickLevel.toFloat(), onValueChange = {
        s.arrangePreferences = p.copy(clickLevel = it.toInt())
    }, valueRange = 0f..200f, modifier = Modifier.fillMaxWidth(),
        colors = SliderDefaults.colors(thumbColor = th.accent, activeTrackColor = th.accent, inactiveTrackColor = th.line))
    MenuNote("100% is the usual click level. The range is 0–200%.")

}

@Composable
private fun SamplerTab(s: ScreenState) {
    val p = s.arrangePreferences
    FieldLabel("STRETCH")
    MenuChoices(listOf("NORMAL" to !p.stretchBeat, "BEAT" to p.stretchBeat)) {
        s.arrangePreferences = p.copy(stretchBeat = it == 1)
    }
    MenuNote("NORMAL works for all material. BEAT uses less CPU and suits drum loops.")
    val map = s.settings[6]
    val follow = s.settings[7]
    FieldLabel("SLICE NOTES")
    MenuChoices(listOf("CHROMATIC" to (map == 0), "SCALE" to (map == 1))) { s.act(Act.SET_SAMPLER_SETTINGS, it, follow) }
    MenuNote("CHROMATIC assigns slices by semitone. SCALE follows the project's scale from its root in octave 3.")
    ToggleRow("WARP TO PROJECT BPM", follow != 0) { s.act(Act.SET_SAMPLER_SETTINGS, map, if (it) 1 else 0) }
    MenuNote("Opening WARP for a sample without a file BPM sets it to the project tempo.")
}

@Composable
private fun ArrangeTab(s: ScreenState) {
    val p = s.arrangePreferences
    FieldLabel("TRACKS")
    MenuChoices(listOf("FIX" to !p.scaleTracks, "SCALE" to p.scaleTracks)) {
        s.arrangePreferences = p.copy(scaleTracks = it == 1)
    }
    MenuNote("FIX uses the Expanded and Compact buttons under the meter. SCALE resizes track rows with a vertical two-finger pinch.")
    ToggleRow("FOLLOW PLAYHEAD", p.follow) { s.arrangePreferences = p.copy(follow = it) }
    MenuNote("Scrolls the arrangement to keep the playhead in view while playing.")
    ToggleRow("ROW REPEATS", p.rowRepeats) { s.arrangePreferences = p.copy(rowRepeats = it) }
    MenuNote("Includes pattern row repeats when sending clips to the arrangement. Turn off for one pass per row.")
}

@Composable
private fun DuckTab(s: ScreenState) {
    val p = s.arrangePreferences
    FieldLabel("DUCK SENDS")
    listOf("REVERB", "DELAY", "DRIVE").forEachIndexed { i, title ->
        val bit = 1 shl i
        ToggleRow(title, p.duckSends and bit != 0) { on ->
            s.arrangePreferences = p.copy(duckSends = if (on) p.duckSends or bit else p.duckSends and bit.inv())
        }
    }
    MenuNote("Duck attenuates enabled effect sends. Existing effect tails keep playing.")
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
    val p = s.arrangePreferences
    ToggleRow("KNOB GROUPS IN COLUMNS", p.knobColumns) { s.arrangePreferences = p.copy(knobColumns = it) }
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
