package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import kotlin.math.abs
import kotlin.math.roundToInt

/** Верхняя панель: бутерброд, BPM, метроном | SCALE, ROOT, UNDO, PLAY, REC, REDO, EQ | PATTERNS, ARRANGE, CPU. */
@Composable
fun TopBar(s: ScreenState, onMenu: () -> Unit) {
    val th = LocalTheme.current
    val top = s.top
    val half = (Layout.topButton - Layout.gap) / 2
    Row(Modifier.fillMaxWidth().height(Layout.topHeight.dp), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        Row(Modifier.width(column()).fillMaxHeight(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            Box(Modifier.width(52.dp).fillMaxHeight().pointerInput(Unit) { detectTapGestures(onTap = { onMenu() }) },
                contentAlignment = Alignment.Center) { Burger(th.text) }
            BpmField(s, top[0] / 10.0, Modifier.weight(1f))
            Metronome(s, top[4] != 0, Modifier.width(52.dp))
        }
        val pi = s.piano
        val dens = density()
        // SCALE и EQ — шириной с PATTERNS (половина правой колонки); остаток — ROOT и кнопке пианоролла.
        val side = ((rightColumn().value - Layout.gap) / 2).dp
        DragField(s, "SCALE", s.topTexts[0], { pi[5] }, { s.act(Act.SET_SCALE, it) },
            Modifier.width(side)) { r ->
            s.choice = Choice(s.scaleNames, 3, pi[5], r) { s.act(Act.SET_SCALE, it) }
        }
        // ROOT: меню (4 столбца) — от левого края ROOT до правого края REDO.
        val rootMenu = 4 * Layout.gap + 2 * Layout.topButton * LocalDims.current.midStretch + 2 * half * LocalDims.current.midStretch
        DragField(s, "ROOT", s.topTexts[1], { pi[6] }, { s.act(Act.SET_ROOT, it) }, Modifier.weight(1f)) { r ->
            s.choice = Choice(s.rootNames, 4, pi[6], r, r.width / dens + rootMenu) { s.act(Act.SET_ROOT, it) }
        }
        Glyph("↶", top[1] != 0, Modifier.width(mid(Layout.topButton)).report(s, "undo")) { s.act(Act.UNDO) }
        PlayButton(s, top[3] != 0, Modifier.width(mid(half)))
        RecButton(s, top.getOrElse(5) { 0 } != 0, Modifier.width(mid(half)))
        Glyph("↷", top[2] != 0, Modifier.width(mid(Layout.topButton))) { s.act(Act.REDO) }
        RollIcon(Modifier.weight(1f))
        Toggle("EQ", s.eq[0] != 0, Modifier.width(side).report(s, "eq")) { s.act(Act.TOGGLE_EQ_VIEW) }
        // PATTERNS и ARRANGE — ровно над правой колонкой, затем место измерителя.
        Row(Modifier.width(rightColumn()).fillMaxHeight(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            Toggle("PATTERNS", s.grid[0] != 0, Modifier.weight(1f).report(s, "patterns")) { s.act(Act.TOGGLE_PATTERNS) }
            Later("ARRANGE", Modifier.weight(1f).report(s, "arrange"))
        }
        Spacer(Modifier.width(Layout.meterWidth.dp))
    }
}

/** Поле с подписью над значением (DragField в Menus.swift): палец вверх-вниз — 24 dp на пункт (после 10 dp; вверх —
 *  больше), одна запись UNDO на жест; тап — меню (onTap получает рамку поля в координатах Body). */
@Composable
private fun DragField(
    s: ScreenState, label: String, value: String, get: () -> Int, set: (Int) -> Unit, modifier: Modifier,
    onTap: (Rect) -> Unit,
) {
    val th = LocalTheme.current
    var active by remember { mutableStateOf(false) }
    var coords by remember { mutableStateOf<LayoutCoordinates?>(null) }
    val getter by rememberUpdatedState(get)
    val setter by rememberUpdatedState(set)
    val tap by rememberUpdatedState(onTap)
    Column(
        modifier.fillMaxHeight().tile(if (active) th.panel2 else th.panel).padding(horizontal = 10.dp)
            .onGloballyPositioned { coords = it }
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    active = true
                    s.act(Act.BEGIN_GESTURE)
                    val start = getter()
                    var moved = false
                    while (true) {
                        val ev = awaitPointerEvent()
                        val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                        val dy = (c.position.y - down.position.y) / density
                        if (abs(dy) > 10) moved = true
                        if (moved) setter(start + (-dy / 24).roundToInt())
                        c.consume()
                        if (!c.pressed) break
                    }
                    s.act(Act.END_GESTURE)
                    active = false
                    if (!moved) {
                        val b = s.body
                        val c = coords
                        if (b != null && c != null && b.isAttached && c.isAttached) tap(b.localBoundingBoxOf(c))
                    }
                }
            },
        verticalArrangement = Arrangement.Center,
    ) {
        Text(label, color = th.dim, fontSize = 9.sp, letterSpacing = 0.8.sp)
        Text(value, color = th.text, fontSize = 15.sp, maxLines = 1)
    }
}

/** Пикселей на dp (плотность раскладки). */
@Composable
private fun density(): Float = androidx.compose.ui.platform.LocalDensity.current.density

/** Рамка кнопки в координатах Body — для строки EQ под верхней панелью. */
private fun Modifier.report(s: ScreenState, key: String): Modifier = this.onGloballyPositioned { c ->
    val b = s.body
    if (b != null && b.isAttached && c.isAttached) {
        val r = b.localBoundingBoxOf(c)
        if (s.topRects[key] != r) s.topRects = s.topRects + (key to r)
    }
}

/** Кнопка окна (PATTERNS, EQ): горит, пока открыто. */
@Composable
private fun Toggle(title: String, on: Boolean, modifier: Modifier, action: () -> Unit) {
    val th = LocalTheme.current
    Box(modifier.fillMaxHeight().tile(if (on) th.text else th.panel2).pointerTap(action), contentAlignment = Alignment.Center) {
        Text(title, color = if (on) th.bg else th.text, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

/** Кнопка следующих этапов: подпись серая, не нажимается. */
@Composable
private fun Later(title: String, modifier: Modifier) {
    val th = LocalTheme.current
    Box(modifier.fillMaxHeight().tile(th.panel2), contentAlignment = Alignment.Center) {
        Text(title, color = th.dim, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
private fun Glyph(symbol: String, enabled: Boolean, modifier: Modifier, action: () -> Unit) {
    val th = LocalTheme.current
    Box(modifier.fillMaxHeight().tile(th.panel2).pointerInput(enabled) { detectTapGestures(onTap = { if (enabled) action() }) },
        contentAlignment = Alignment.Center) {
        Text(symbol, color = if (enabled) th.text else th.dim, fontSize = 22.sp, fontWeight = FontWeight.Bold)
    }
}

/** PLAY срабатывает на касании, а не на отпускании. */
@Composable
private fun PlayButton(s: ScreenState, on: Boolean, modifier: Modifier) {
    val th = LocalTheme.current
    Box(
        modifier.fillMaxHeight().framed(if (on) th.accent else th.panel2, th.accent)
            .pointerInput(Unit) { awaitEachGesture { awaitFirstDown(); s.act(Act.TOGGLE_PLAY) } },
        contentAlignment = Alignment.Center,
    ) { Text("▶", color = if (on) th.onAccent else th.accent, fontSize = 16.sp, fontWeight = FontWeight.Bold) }
}

/** REC: красная точка и красная окантовка; включена — горит красным. С PLAY ноты клавиатуры пишутся в секвенсор. */
@Composable
private fun RecButton(s: ScreenState, on: Boolean, modifier: Modifier) {
    val th = LocalTheme.current
    Box(
        modifier.fillMaxHeight().framed(if (on) th.rec else th.panel2, th.rec).pointerTap { s.act(Act.TOGGLE_REC) },
        contentAlignment = Alignment.Center,
    ) { Text("●", color = if (on) Color.White else th.rec, fontSize = 16.sp) }
}

/** BPM: палец вверх-вниз — 4 dp на 1 BPM (после 10 dp), одна запись UNDO на жест. */
@Composable
private fun BpmField(s: ScreenState, bpm: Double, modifier: Modifier) {
    val th = LocalTheme.current
    var active by remember { mutableStateOf(false) }
    val text = if (bpm == Math.rint(bpm)) "${bpm.toInt()}" else String.format(java.util.Locale.US, "%.1f", bpm)
    Row(
        modifier.fillMaxHeight().tile(if (active) th.panel2 else th.panel).padding(horizontal = 12.dp)
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    active = true
                    s.act(Act.BEGIN_GESTURE)
                    val start = s.top[0] / 10.0
                    var moved = false
                    while (true) {
                        val ev = awaitPointerEvent()
                        val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                        val dy = (c.position.y - down.position.y) / density
                        if (abs(dy) > 10) moved = true
                        if (moved) s.act(Act.SET_BPM, 0, 0, (start - dy / 4).roundToInt().toDouble())
                        c.consume()
                        if (!c.pressed) break
                    }
                    s.act(Act.END_GESTURE)
                    active = false
                }
            },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("BPM", color = th.dim, fontSize = 11.sp, letterSpacing = 0.9.sp)
        Spacer(Modifier.weight(1f))
        Text(text, color = th.text, fontSize = 19.sp)
    }
}

/** Метроном: тап включает; доля мигает (акцент — мягким жёлтым). */
@Composable
private fun Metronome(s: ScreenState, on: Boolean, modifier: Modifier) {
    val th = LocalTheme.current
    val bg = if (s.beatFlash != 0) (if (s.beatAccent != 0) th.accentSoft else th.panel2) else th.panel
    val fg = if (on) th.text else th.dim
    Box(
        modifier.fillMaxHeight().tile(bg).border(1.dp, if (on) th.text else Color.Transparent, RoundedCornerShape(Layout.radius.dp))
            .pointerInput(Unit) { detectTapGestures(onTap = { s.act(Act.TOGGLE_METRONOME) }) },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(20.dp)) {
            val w = size.width
            val h = size.height
            val body = Path().apply {
                moveTo(w * 0.35f, h * 0.05f); lineTo(w * 0.65f, h * 0.05f); lineTo(w * 0.9f, h * 0.95f)
                lineTo(w * 0.1f, h * 0.95f); close()
            }
            drawPath(body, fg, style = Stroke(1.6.dp.toPx()))
            drawLine(fg, Offset(w * 0.5f, h * 0.75f), Offset(w * 0.8f, h * 0.2f), 1.6.dp.toPx(), StrokeCap.Round)
        }
    }
}

/** Бутерброд — только полоски, без фона. */
@Composable
fun Burger(color: Color) {
    Column(verticalArrangement = Arrangement.spacedBy(5.dp)) {
        repeat(3) { Box(Modifier.width(22.dp).height(2.dp).tile(color, 1f)) }
    }
}

/** ROLL — окно пианоролла (значок клавиш, как pianokeys в Swift-версии); окно — позже, пока серая. */
@Composable
private fun RollIcon(modifier: Modifier) {
    val th = LocalTheme.current
    Box(modifier.fillMaxHeight().tile(th.panel2), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(width = 22.dp, height = 18.dp)) {
            val w = size.width
            val h = size.height
            val sw = 1.6.dp.toPx()
            drawRect(th.dim, style = Stroke(sw))
            for (k in 1..2) drawLine(th.dim, Offset(w * k / 3, h * 0.55f), Offset(w * k / 3, h), sw)
            for (k in 1..2) drawRect(th.dim, Offset(w * k / 3 - w * 0.09f, 0f), androidx.compose.ui.geometry.Size(w * 0.18f, h * 0.55f))
        }
    }
}
