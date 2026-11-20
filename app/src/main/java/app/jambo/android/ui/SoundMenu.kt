package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act

/**
 * Меню звука дорожки (SoundMenu.swift ветки swift JAMbo): на месте левой колонки. Две колонки движков — DRUMS (под
 * ними SAMPLER) и SYNTH: тап — движок дорожки, с зажатыми шагами — их звук. Внизу группа LOAD SAMPLE · PRESET · MIDI:
 * LOAD (браузер библиотеки), MIDI (для шагов — TRACK: вернуть звук дорожки), SAVE, TABLE (у GRANULAR — SOURCE). MIDI,
 * SAVE, TABLE и SOURCE — позже.
 */
@Composable
fun SoundMenu(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val shape = RoundedCornerShape(Layout.radius.dp)
    val snd = s.sound
    val steps = snd.copyOfRange(6, 6 + snd[5]).toList()
    val forSteps = steps.isNotEmpty()
    val trackEngine = snd[3]
    val e = s.engines
    val drums = e.copyOfRange(1, 1 + e[0]).toList()
    val synths = e.copyOfRange(2 + e[0], 2 + e[0] + e[1 + e[0]]).toList()
    Box(modifier.shadow(12.dp, shape).background(th.bg, shape).border(1.dp, th.line, shape).padding(6.dp)) {
        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (forSteps) MenuTitle("STEP " + steps.joinToString(", ") { (it + 1).toString() })
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                EngineColumn(s, "DRUMS", drums, if (forSteps) -1 else trackEngine, Modifier.weight(1f)) {
                    MenuItem("SAMPLER", enabled = !forSteps, current = !forSteps && trackEngine < 0 && snd[4] == 0) {
                        s.act(Act.CLEAN_SAMPLER)
                    }
                }
                EngineColumn(s, "SYNTH", synths, if (forSteps) -1 else trackEngine, Modifier.weight(1f)) {}
            }
            Spacer(Modifier.weight(1f))
            MenuTitle("LOAD SAMPLE · PRESET · MIDI")
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem("LOAD", Modifier.weight(1f), current = !forSteps && snd[4] != 0) { s.libAct(app.jambo.android.LibAct.OPEN) }
                if (forSteps) MenuItem("TRACK", Modifier.weight(1f)) { s.act(Act.STEP_SOUND_TO_TRACK) }
                else MenuItem("MIDI", Modifier.weight(1f), enabled = false) {}
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem("SAVE", Modifier.weight(1f), enabled = false) {}
                MenuItem(if (trackEngine == Engines.granular) "SOURCE" else "TABLE", Modifier.weight(1f), enabled = false) {}
            }
        }
    }
}

/** Номера своих движков (Jambo.swift). */
object Engines {
    const val wtable = 19
    const val granular = 26
}

@Composable
private fun EngineColumn(
    s: ScreenState, name: String, ids: List<Int>, current: Int, modifier: Modifier, extra: @Composable () -> Unit,
) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        MenuTitle(name)
        for (id in ids) MenuItem(s.engineNames.getOrElse(id) { "?" }, current = current == id) { s.act(Act.PICK_SYNTH, id) }
        extra()
    }
}

@Composable
private fun MenuTitle(text: String) {
    val th = LocalTheme.current
    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Text(text, color = th.dim, fontSize = 10.tsp(), fontWeight = FontWeight.Bold, letterSpacing = 0.4.sp, maxLines = 1)
    }
}

/** Кнопка меню (MenuItem.swift): высота 40, текущая — жёлтая, неактивная — серая. */
@Composable
fun MenuItem(
    title: String, modifier: Modifier = Modifier, enabled: Boolean = true, red: Boolean = false, current: Boolean = false,
    action: () -> Unit,
) {
    val th = LocalTheme.current
    val fg = if (!enabled) th.off else if (current) th.onAccent else if (red) th.rec else th.text
    val tap = if (enabled) Modifier.pointerTap(action) else Modifier
    Box(
        modifier.fillMaxWidth().heightIn(min = 40.dp).tile(if (current) th.accent else th.panel, 8f).then(tap),
        contentAlignment = Alignment.Center,
    ) {
        Text(title, color = fg, fontSize = 13.tsp(), fontWeight = FontWeight.Bold, maxLines = 1)
    }
}
