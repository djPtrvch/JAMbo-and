package app.jambo.android.ui

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import app.jambo.android.MidiTarget

/** Зазор между клавишами. */
object Keyboard { const val GAP = 3f }

private const val KEY_GAP = Keyboard.GAP

/** Клавиатура: CHORD и бутерброд (меню клавиатуры) | клавиши гаммы (с CHORD — подписи аккордов) | ▲ ▼ октавы. */
@Composable
fun KeyboardPanel(s: ScreenState, width: Float) {
    val th = LocalTheme.current
    // Сторона клавиши — по двум октавам гаммы (с одной октавой клавиши шире, высота та же).
    val count = s.keys.getOrElse(1) { 15 }
    val step = Layout.step(width)
    // Высота клавиатуры — как на iPad (по его рабочей ширине): на широком экране центр не сжимается.
    val base = Dims.ipadWidth
    Row(Modifier.fillMaxWidth().height(Layout.pianoHeight(base, count).dp),
        horizontalArrangement = Arrangement.spacedBy(Layout.stepGap.dp)) {
        Column(Modifier.width(step.dp).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            ChordButton(s, Modifier.fillMaxWidth().height(Layout.keyHeight(base, count).dp))
            Box(Modifier.fillMaxWidth().weight(1f).pointerTap { s.pianoMenu = !s.pianoMenu },
                contentAlignment = Alignment.Center) { Burger(th.text) }
        }
        KeyRow(s, Modifier.weight(1f).fillMaxHeight().onGloballyPositioned { c ->
            s.body?.let { b -> if (b.isAttached) s.keysRect = b.localBoundingBoxOf(c) }
        })
        OctavePair(s, Modifier.width(step.dp).fillMaxHeight())
    }
}

/** CHORD / ARP / RANDOM: долгое нажатие открывает настройки текущего режима. */
@Composable
private fun ChordButton(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val arp = s.arp[0] != 0
    val random = s.arp[8] != 0
    val pressed = remember { mutableStateOf(false) }
    val on = when {
        arp -> s.arp[1] != 0
        random -> pressed.value
        else -> s.piano[0] != 0
    }
    Column(
        modifier.midiLearnMark(s, MidiTarget.CHORD).tile(if (on) th.text else th.panel2).pointerInput(Unit) {
            detectTapGestures(
                onPress = {
                    pressed.value = true
                    try { tryAwaitRelease() } finally { pressed.value = false }
                },
                onTap = { if (!s.learnTarget(MidiTarget.CHORD)) s.act(Act.TOGGLE_CHORD) },
                onLongPress = { s.chordMenu = true },
            )
        },
        horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center,
    ) {
        val fg = if (on) th.bg else th.text
        Text(if (arp) "ARP" else if (random) "RANDOM" else "CHORD", color = fg, fontSize = 12.tsp(), fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
        val sub = when {
            arp -> arpDirections[s.arp[2]]
            s.arp[7] >= 0 -> if (random) randomStyles[s.arp[7]] else ""
            else -> s.pianoTexts[0]
        }
        Text(sub, color = fg, fontSize = 10.tsp())
    }
}

@Composable
private fun KeyRow(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val keys = s.keys
    val count = keys[0]
    val focused = LocalWindowInfo.current.isWindowFocused
    Row(
        modifier.pointerInput(count, focused) {
            if (!focused || count == 0) return@pointerInput
            val touches = HashMap<PointerId, Int>()
            var serial = 0
            try {
                awaitPointerEventScope {
                    while (true) {
                        val ev = awaitPointerEvent()
                        for (c in ev.changes) {
                            if (c.changedToDown()) {
                                val cell = (size.width + KEY_GAP * density) / count
                                val k = (c.position.x / cell).toInt().coerceIn(0, count - 1)
                                if (s.learnTarget(MidiTarget.KEY + k)) {
                                    c.consume()
                                    continue
                                }
                                val id = ++serial
                                touches[c.id] = id
                                // Сила (KEY VELOCITY) — по месту на клавише: верх — тихо, низ — громко.
                                val y = (c.position.y / maxOf(1, size.height)).coerceIn(0f, 1f)
                                s.act(Act.KEY_DOWN, id, k, 0.2 + 0.8 * y)
                            } else if (!c.pressed) {
                                // При отмене Android может прислать уже поглощённое отпускание.
                                touches.remove(c.id)?.let { s.act(Act.KEY_UP, it) }
                            }
                            c.consume()
                        }
                    }
                }
            } finally {
                for (id in touches.values) s.act(Act.KEY_UP, id)
            }
        },
        horizontalArrangement = Arrangement.spacedBy(KEY_GAP.dp),
    ) {
        for (k in 0 until count) {
            val bits = keys.getOrElse(3 + 2 * k) { 0 }
            val on = bits and 1 != 0
            val shown = bits and 2 != 0
            val root = bits and 4 != 0
            val bg = if (on) th.text else if (root) th.root else th.panel
            val fg = if (on) th.bg else if (shown || root) th.text else th.dim
            Box(
                Modifier.weight(1f).fillMaxHeight().midiLearnMark(s, MidiTarget.KEY + k, 6f).tile(bg, 6f)
                    .border(1.dp, if (shown && !on) th.text else Color.Transparent, RoundedCornerShape(6.dp)),
                contentAlignment = Alignment.BottomCenter,
            ) {
                Text(s.keyLabels.getOrElse(k) { "" }, color = fg, fontSize = 11.tsp(), textAlign = TextAlign.Center,
                    maxLines = 2, modifier = Modifier.padding(start = 2.dp, end = 2.dp, bottom = 6.dp))
            }
        }
    }
}

@Composable
private fun OctavePair(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    Column(modifier.pointerInput(Unit) {
        awaitPointerEventScope {
            while (true) {
                val ev = awaitPointerEvent()
                for (c in ev.changes) {
                    if (c.changedToUp()) {
                        val up = c.position.y < size.height / 2
                        val target = if (up) MidiTarget.OCTAVE_UP else MidiTarget.OCTAVE_DOWN
                        if (!s.learnTarget(target)) s.act(Act.OCTAVE_STEP, if (up) 1 else -1)
                    }
                    c.consume()
                }
            }
        }
    }, verticalArrangement = Arrangement.spacedBy(Layout.stepGap.dp)) {
        for ((i, t) in listOf("▲", "▼").withIndex()) {
            Box(Modifier.weight(1f).fillMaxWidth().midiLearnMark(s, MidiTarget.OCTAVE_UP + i).tile(th.panel2), contentAlignment = Alignment.Center) {
                Text(t, color = th.text, fontSize = 14.isp())
            }
        }
    }
}
