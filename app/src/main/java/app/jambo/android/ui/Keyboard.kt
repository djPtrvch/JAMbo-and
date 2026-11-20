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
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act

private const val KEY_GAP = 3f

/** Клавиатура: CHORD и бутерброд | клавиши гаммы | ▲ ▼ октавы. CHORD и меню клавиатуры — позже (серые). */
@Composable
fun KeyboardPanel(s: ScreenState, width: Float) {
    val th = LocalTheme.current
    // Сторона клавиши — по двум октавам гаммы (с одной октавой клавиши шире, высота та же).
    val count = s.keys.getOrElse(1) { 15 }
    val step = Layout.step(width)
    // Высота клавиатуры — как на iPad (по ширине без растяжения): центр не сжимается на широком экране.
    val base = width / LocalStretch.current
    Row(Modifier.fillMaxWidth().height(Layout.pianoHeight(base, count).dp),
        horizontalArrangement = Arrangement.spacedBy(Layout.stepGap.dp)) {
        Column(Modifier.width(step.dp).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            Box(Modifier.fillMaxWidth().height(Layout.keyHeight(base, count).dp).tile(th.panel2), contentAlignment = Alignment.Center) {
                Text("CHORD", color = th.dim, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
            }
            Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) { Burger(th.dim) }
        }
        KeyRow(s, Modifier.weight(1f).fillMaxHeight())
        OctavePair(s, Modifier.width(step.dp).fillMaxHeight())
    }
}

@Composable
private fun KeyRow(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val keys = s.keys
    val count = keys[0]
    val touches = remember { HashMap<PointerId, Int>() }
    Row(
        modifier.pointerInput(count) {
            awaitPointerEventScope {
                var serial = 0
                while (true) {
                    val ev = awaitPointerEvent()
                    for (c in ev.changes) {
                        if (c.changedToDown()) {
                            val cell = (size.width + KEY_GAP * density) / maxOf(1, count)
                            val k = (c.position.x / cell).toInt().coerceIn(0, count - 1)
                            val id = ++serial
                            touches[c.id] = id
                            // Сила — по месту на клавише (верх — тихо, низ — громко); VELOCITY — позже.
                            val y = (c.position.y / maxOf(1, size.height)).coerceIn(0f, 1f)
                            s.act(Act.KEY_DOWN, id, k, 0.2 + 0.8 * y)
                        } else if (c.changedToUp()) {
                            touches.remove(c.id)?.let { s.act(Act.KEY_UP, it) }
                        }
                        c.consume()
                    }
                }
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
                Modifier.weight(1f).fillMaxHeight().tile(bg, 6f)
                    .border(1.dp, if (shown && !on) th.text else Color.Transparent, RoundedCornerShape(6.dp)),
                contentAlignment = Alignment.BottomCenter,
            ) {
                Text(s.keyLabels.getOrElse(k) { "" }, color = fg, fontSize = 11.sp, textAlign = TextAlign.Center,
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
                    if (c.changedToUp()) s.act(Act.OCTAVE_STEP, if (c.position.y < size.height / 2) 1 else -1)
                    c.consume()
                }
            }
        }
    }, verticalArrangement = Arrangement.spacedBy(Layout.stepGap.dp)) {
        for (t in listOf("▲", "▼")) {
            Box(Modifier.weight(1f).fillMaxWidth().tile(th.panel2), contentAlignment = Alignment.Center) {
                Text(t, color = th.text, fontSize = 14.sp)
            }
        }
    }
}
