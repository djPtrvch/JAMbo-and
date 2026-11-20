package app.jambo.android.ui

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act

/** Рамка всплывающего меню (menuBox в Swift-версии). */
@Composable
fun Modifier.menuBox(): Modifier {
    val th = LocalTheme.current
    val shape = RoundedCornerShape(9.dp)
    return this.shadow(12.dp, shape).background(th.bg, shape).border(1.dp, th.line, shape)
}

/** Меню выбора (SCALE, ROOT): сетка кнопок под полем (не помещается — над ним); тап мимо — закрыть. */
@Composable
fun ChoiceOverlay(s: ScreenState, c: Choice) {
    val d = LocalDensity.current.density
    val screenW = (s.body?.size?.width ?: 0) / d
    val screenH = (s.body?.size?.height ?: 0) / d
    val width = c.width ?: (c.columns * 104f)
    val rows = (c.options.size + c.columns - 1) / c.columns
    val height = rows * 48f + 12
    val a = c.anchor
    val x = minOf(maxOf(8f, a.left / d), screenW - width - 8)
    val below = a.bottom / d + 6 + height < screenH
    val y = if (below) a.bottom / d + 6 else maxOf(8f, a.top / d - 6 - height)
    Box(Modifier.fillMaxSize().pointerTap { s.choice = null })
    Column(
        Modifier.offset(x.dp, y.dp).width(width.dp).menuBox().pointerInput(Unit) { detectTapGestures { } }.padding(6.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        for (r in 0 until rows) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (col in 0 until c.columns) {
                    val i = r * c.columns + col
                    if (i < c.options.size) {
                        InkButton(c.options[i], i == c.current, Modifier.weight(1f), 44f) {
                            s.choice = null
                            c.pick(i)
                        }
                    } else Spacer(Modifier.weight(1f))
                }
            }
        }
    }
}

/** Окно CHORD (долгое нажатие на CHORD; ChordMenu.swift): типы аккорда квадратными кнопками в 2 столбца, от левого
 *  нижнего угла первой клавиши до верха строки дорожек; тап — выбор и закрыть; тап мимо — закрыть. */
@Composable
fun ChordMenuOverlay(s: ScreenState) {
    val d = LocalDensity.current.density
    val keys = s.keysRect
    val pad = 10f
    val gap = 6f
    val head = 14f
    val height = maxOf(160f, (keys.bottom - s.tracksRect.top) / d)
    val rowH = maxOf(28f, (height - 2 * pad - head - 3 * gap) / 3)
    val width = 2 * rowH + gap + 2 * pad
    val names = s.chordKindNames
    Box(Modifier.fillMaxSize().pointerTap { s.chordMenu = false })
    Column(
        Modifier.offset((keys.left / d).dp, (keys.bottom / d - height).dp).width(width.dp).height(height.dp).menuBox()
            .pointerInput(Unit) { detectTapGestures { } }.padding(pad.dp),
        verticalArrangement = Arrangement.spacedBy(gap.dp),
    ) {
        Box(Modifier.height(head.dp), contentAlignment = Alignment.BottomStart) { FieldLabel("CHORD") }
        for (r in 0 until (names.size + 1) / 2) {
            Row(horizontalArrangement = Arrangement.spacedBy(gap.dp)) {
                for (col in 0 until 2) {
                    val k = r * 2 + col
                    if (k < names.size) {
                        InkButton(names[k], k == s.piano[1], Modifier.width(rowH.dp), rowH) {
                            s.act(Act.SET_CHORD_KIND, k)
                            s.chordMenu = false
                        }
                    }
                }
            }
        }
    }
}

/** Меню клавиатуры (бутерброд под CHORD; PianoMenu в Menus.swift): 1 / 2 OCTAVES, SWING, KEY VELOCITY, OVERDUB;
 *  от левого нижнего угла первой клавиши вверх, не выше верха строки дорожек (дальше — прокрутка), шириной 4 шага.
 *  ARPEGGIATOR — с режимом ARP, позже. */
@Composable
fun PianoMenuOverlay(s: ScreenState, step: Float) {
    val d = LocalDensity.current.density
    val keys = s.keysRect
    val width = 4 * step + 3 * Layout.stepGap
    val room = maxOf(160f, (keys.bottom - s.tracksRect.top) / d - 20)
    val p = s.piano
    Box(Modifier.fillMaxSize().pointerTap { s.pianoMenu = false })
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopStart) {
        Column(
            Modifier.offset((keys.left / d).dp, 0.dp).width(width.dp).anchorBottom(keys.bottom / d)
                .heightIn(max = (room + 20).dp).menuBox().pointerInput(Unit) { detectTapGestures { } }
                .verticalScroll(rememberScrollState()).padding(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                InkButton("1 OCTAVE", p[2] == 1, Modifier.weight(1f), 44f) { s.act(Act.SET_PIANO_SETTINGS, 1, p[3]) }
                InkButton("2 OCTAVES", p[2] == 2, Modifier.weight(1f), 44f) { s.act(Act.SET_PIANO_SETTINGS, 2, p[3]) }
            }
            SwingSlider(s)
            ToggleRow("KEY VELOCITY", p[3] != 0) { s.act(Act.SET_PIANO_SETTINGS, p[2], if (it) 1 else 0) }
            Hint(if (p[3] != 0) "Lower on the key — louder. Scales the track Velocity; REC writes it as the note velocity."
                else "Keys play at the track Velocity.")
            ToggleRow("OVERDUB", p[4] != 0) { s.act(Act.SET_RECORD_SETTINGS, 1, if (it) 1 else 0) }
            Hint(if (p[4] != 0) "REC adds new notes to the pattern."
                else "REC clears the steps it passes over; only the new notes stay.")
        }
    }
}

/** Низ — на bottom (dp от верха родителя), высота — по содержимому. */
private fun Modifier.anchorBottom(bottom: Float): Modifier = this.layout { m, c ->
    val pl = m.measure(c.copy(minHeight = 0))
    layout(pl.width, pl.height) { pl.place(0, (bottom * density).toInt() - pl.height) }
}

@Composable
private fun Hint(text: String) {
    Text(text, color = LocalTheme.current.dim, fontSize = 11.sp)
}

@Composable
fun FieldLabel(title: String) {
    Text(title, color = LocalTheme.current.dim, fontSize = 9.sp, letterSpacing = 0.8.sp)
}

/** Вкл./выкл. — рычажок, как в iOS (строка 48, подпись слева, переключатель 51×31: выключен — серый, включён —
 *  жёлтый, бегунок переезжает за 0,2 с); тап по строке переключает. */
@Composable
fun ToggleRow(title: String, on: Boolean, change: (Boolean) -> Unit) {
    val th = LocalTheme.current
    val x by animateDpAsState(if (on) 22.dp else 2.dp, label = "toggle")
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp).pointerTap { change(!on) }.padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text(title, color = th.text, fontSize = 13.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
        Box(Modifier.size(51.dp, 31.dp).background(if (on) th.accent else th.meterTrack, RoundedCornerShape(16.dp))) {
            Box(Modifier.offset(x, 2.dp).size(27.dp).shadow(2.dp, CircleShape).background(Color.White, CircleShape))
        }
    }
}

/** SWING дорожки: ползунок с процентами, палец вбок; один жест — одна запись UNDO. */
@Composable
private fun SwingSlider(s: ScreenState) {
    val th = LocalTheme.current
    val p = s.piano
    val lo = p[8] / 10.0
    val hi = p[9] / 10.0
    val n = if (hi > lo) ((p[7] / 10.0 - lo) / (hi - lo)).toFloat().coerceIn(0f, 1f) else 0f
    Column(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row {
            Text("SWING", color = th.text, fontSize = 13.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            Text(s.pianoTexts[1], color = th.text, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }
        BoxWithConstraints(Modifier.fillMaxWidth().height(24.dp).pointerInput(lo, hi) {
            awaitEachGesture {
                val down = awaitFirstDown()
                s.act(Act.BEGIN_GESTURE)
                fun set(px: Float) {
                    val k = (px / size.width.coerceAtLeast(1)).coerceIn(0f, 1f)
                    s.act(Act.SET_SWING, 0, 0, Math.rint(lo + (hi - lo) * k))
                }
                set(down.position.x)
                while (true) {
                    val ev = awaitPointerEvent()
                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                    set(c.position.x)
                    c.consume()
                    if (!c.pressed) break
                }
                s.act(Act.END_GESTURE)
            }
        }, contentAlignment = Alignment.CenterStart) {
            val w = maxWidth.value
            val x = w * n
            Box(Modifier.fillMaxWidth().height(6.dp).background(th.meterTrack, CircleShape))
            Box(Modifier.width(maxOf(6f, x).dp).height(6.dp).background(th.accent, CircleShape))
            Box(Modifier.offset(minOf(maxOf(0f, x - 11), w - 22).dp, 0.dp).size(22.dp).shadow(2.dp, CircleShape)
                .background(Color.White, CircleShape))
        }
    }
}
