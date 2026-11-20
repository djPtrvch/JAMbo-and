package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import app.jambo.android.AppNative.Act
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Что тянет палец по миниатюрам: верхний или нижний край обводки, обводку целиком или новый диапазон. */
private enum class DragMode { TOP, BOTTOM, MOVE, RANGE }

/** Касание колонки миниатюр (PagePress в PagesColumn.swift). */
private class PagePress(val row: Int, val plus: Boolean, val y0: Float, val from0: Int, val to0: Int, val mode: DragMode) {
    var moved = false
    var held = false
    var lifting = false
    var liftTo = row
    var liftY = y0
}

/**
 * Миниатюры страниц (PagesColumn.swift ветки swift JAMbo): строками, под ними «+». Белая обводка — играющие страницы,
 * светлее — открытая в секвенсоре. Тап (через 0,3 с, если не было второго) — эта страница играет и открыта;
 * протягивание — несколько играющих (у края обводки — край, в середине — обводка целиком); удержание (0,45 с) —
 * открыть страницу, не меняя играющих, и протягивание — перенос; двойной тап — меню страницы.
 */
@Composable
fun PagesColumn(s: ScreenState, modifier: Modifier) {
    val scope = rememberCoroutineScope()
    var press by remember { mutableStateOf<PagePress?>(null) }
    var liftY by remember { mutableStateOf(0f) }
    var liftTo by remember { mutableStateOf(-1) }
    val g = remember { PageGestures() }
    BoxWithConstraints(modifier) {
        val h = maxHeight.value
        val rowH = (h - SPACING * (ROWS - 1)) / ROWS
        val step = rowH + SPACING
        val pg = s.pages
        val n = pg[0]
        Box(Modifier.fillMaxSize().pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                val y0 = down.position.y / density
                val p = g.begin(s, y0, step, scope) { cur ->
                    press = cur
                    liftTo = cur.liftTo
                    liftY = cur.liftY
                } ?: return@awaitEachGesture
                press = p
                while (true) {
                    val ev = awaitPointerEvent()
                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                    if (!c.pressed) break
                    val y = c.position.y / density
                    g.move(s, p, y, step)
                    if (p.lifting) {
                        liftY = p.liftY
                        liftTo = p.liftTo
                    }
                    c.consume()
                }
                g.end(s, p, step, h, scope)
                press = null
                liftTo = -1
            }
        }) {
            for (r in 0 until ROWS) {
                val lifted = press?.lifting == true && press?.row == r
                val dy = rowOffset(r, press, liftY, liftTo, step)
                val mod = Modifier.fillMaxWidth().height(rowH.dp).offset(y = (r * step + dy).dp)
                    .zIndex(if (lifted) 1f else 0f)
                    .then(if (lifted) Modifier.scale(1.06f).shadow(8.dp, RoundedCornerShape(6.dp)) else Modifier)
                when {
                    r < n -> PageThumb(s, r, mod)
                    r == n && n < MAX_PAGES -> PlusThumb(mod)
                }
            }
            // Белая обводка играющих страниц.
            val th = LocalTheme.current
            val top = pg[1] * step - 3
            val oh = (pg[2] - pg[1] + 1) * step - SPACING + 6
            Box(Modifier.fillMaxWidth().offset(x = (-3).dp, y = top.dp).height(oh.dp).widerBy(6f)
                .border(1.dp, th.text, RoundedCornerShape(8.dp)))
        }
    }
}

/** Шире на extra точек (обводка выходит за колонку на 3 с каждой стороны). */
private fun Modifier.widerBy(extra: Float): Modifier = this.layout { m, c ->
    val w = c.maxWidth + (extra * density).roundToInt()
    val pl = m.measure(c.copy(minWidth = w, maxWidth = w))
    layout(c.maxWidth, pl.height) { pl.place(0, 0) }
}

private fun rowOffset(r: Int, p: PagePress?, liftY: Float, liftTo: Int, step: Float): Float {
    if (p == null || !p.lifting) return 0f
    if (r == p.row) return liftY - p.y0
    if (p.row < liftTo && r > p.row && r <= liftTo) return -step
    if (liftTo < p.row && r >= liftTo && r < p.row) return step
    return 0f
}

private const val ROWS = 8
private const val SPACING = 4f
private const val MAX_PAGES = 8

/** Логика касаний колонки (begin, move, end — как в PagesColumn.swift). */
private class PageGestures {
    var holdJob: Job? = null
    var tapJob: Job? = null
    var lastTapRow = -1
    var lastTapAt = 0L

    fun begin(s: ScreenState, y: Float, step: Float, scope: CoroutineScope, onHold: (PagePress) -> Unit): PagePress? {
        val pg = s.pages
        val n = pg[0]
        val r = max(0, (y / step).toInt())
        val mode = dragMode(pg, y, step)
        if (r > n && mode == DragMode.RANGE) return null
        val p = PagePress(min(r, n), r == n && mode == DragMode.RANGE, y, pg[1], pg[2], mode)
        val nearEdge = edgeDistance(pg, y, step) < 0.25f * (step - SPACING)
        holdJob?.cancel()
        holdJob = scope.launch {
            delay(450)
            if (p.moved || p.plus || nearEdge) return@launch
            p.held = true
            // Страница открыта в секвенсоре и поднята: протягивание переносит её.
            p.lifting = p.row < s.pages[0]
            p.liftTo = p.row
            p.liftY = p.y0
            s.act(Act.OPEN_PAGE, p.row)
            onHold(p)
        }
        return p
    }

    fun move(s: ScreenState, p: PagePress, y: Float, step: Float) {
        if (p.lifting) {
            p.liftY = y
            p.liftTo = ((y / step).toInt()).coerceIn(0, s.pages[0] - 1)
            if (abs(y - p.y0) > 8) p.moved = true
            return
        }
        if (!p.moved && abs(y - p.y0) > 8) {
            p.moved = true
            holdJob?.cancel()
            s.act(Act.BEGIN_GESTURE)
        }
        if (p.moved && !p.plus && !p.held) follow(s, p, y, step)
    }

    fun end(s: ScreenState, p: PagePress, step: Float, height: Float, scope: CoroutineScope) {
        holdJob?.cancel()
        if (p.lifting) {
            if (p.liftTo != p.row) s.act(Act.MOVE_PAGE, p.row, p.liftTo)
            return
        }
        if (p.moved) {
            s.act(Act.END_GESTURE)
            return
        }
        if (p.held) return
        if (p.plus) {
            s.act(Act.ADD_PAGE)
            return
        }
        if (p.row < s.pages[0]) tap(s, p.row, step, height, scope)
    }

    /** Тап: второй за 0,3 с — меню страницы; иначе через 0,3 с — эта страница играет. */
    private fun tap(s: ScreenState, r: Int, step: Float, height: Float, scope: CoroutineScope) {
        val now = System.currentTimeMillis()
        if (lastTapRow == r && now - lastTapAt < 300) {
            tapJob?.cancel()
            lastTapRow = -1
            s.pageMenuY = min((r + 1) * step, height)
            s.pageMenu = r
            return
        }
        lastTapRow = r
        lastTapAt = now
        tapJob?.cancel()
        tapJob = scope.launch {
            delay(300)
            s.act(Act.PLAY_PAGES, r, r)
        }
    }

    /** Протягивание: край идёт за пальцем (страница берётся, когда край зашёл в неё на треть), обводка целиком —
     *  сдвигом на целые страницы, снаружи — диапазон от первой страницы до пальца. */
    private fun follow(s: ScreenState, p: PagePress, y: Float, step: Float) {
        val pg = s.pages
        val n = pg[0]
        val rowH = step - SPACING
        var from = pg[1]
        var to = pg[2]
        when (p.mode) {
            DragMode.TOP -> {
                while (from < to && y > from * step + rowH / 3) from += 1
                while (from > 0 && y < (from - 1) * step + rowH * 2 / 3) from -= 1
            }
            DragMode.BOTTOM -> {
                while (to > from && y < to * step + rowH * 2 / 3) to -= 1
                while (to < n - 1 && y > (to + 1) * step + rowH / 3) to += 1
            }
            DragMode.MOVE -> {
                val len = p.to0 - p.from0
                val d = ((y - p.y0) / step).roundToInt()
                from = min(max(0, p.from0 + d), n - 1 - len)
                to = from + len
            }
            DragMode.RANGE -> {
                val r = min(max(0, (y / step).toInt()), n - 1)
                val a = min(p.row, n - 1)
                from = min(a, r)
                to = max(a, r)
            }
        }
        if (from != pg[1] || to != pg[2]) s.act(Act.PLAY_PAGES, from, to)
    }

    private fun edgeDistance(pg: IntArray, y: Float, step: Float): Float {
        val top = pg[1] * step
        val bottom = (pg[2] + 1) * step - SPACING
        return min(abs(y - top), abs(y - bottom))
    }

    private fun dragMode(pg: IntArray, y: Float, step: Float): DragMode {
        val rowH = step - SPACING
        val from = pg[1]
        val top = from * step
        val bottom = (pg[2] + 1) * step - SPACING
        val grab = 0.4f * rowH
        // Обводка от первой страницы: внутрь зона вдвое шире; выше верха колонки — тоже верхний край.
        val grabTop = if (from == 0) 0.8f * rowH else grab
        val dTop = abs(y - top)
        val dBottom = abs(y - bottom)
        val inTop = dTop < grabTop || (from == 0 && y < 0)
        val inBottom = dBottom < grab
        if (inTop && inBottom) return if (dBottom <= dTop && from != 0 || dBottom < dTop) DragMode.BOTTOM else DragMode.TOP
        if (inBottom) return DragMode.BOTTOM
        if (inTop) return DragMode.TOP
        if (y > top && y < bottom) return DragMode.MOVE
        return DragMode.RANGE
    }
}

/** Миниатюра страницы: 16 ячеек (пустые серые, триги белые, хвосты полупрозрачные; играющий шаг — жёлтый контур,
 *  за длиной — бледные); открытая в секвенсоре — светлее. */
@Composable
private fun PageThumb(s: ScreenState, page: Int, modifier: Modifier) {
    val th = LocalTheme.current
    val pg = s.pages
    val base = 7 + page * 17
    val current = page in pg[3]..pg[4]
    val pos = if (s.playPos >= 0) s.playPos - page * 16 else -1
    Canvas(modifier.background(if (current) th.panel2 else th.panel, RoundedCornerShape(6.dp)).padding(4.dp)) {
        val w = (size.width - 2.dp.toPx() * 15) / 16
        val gap = 2.dp.toPx()
        val rad = CornerRadius(2.dp.toPx())
        for (c in 0 until 16) {
            val bits = pg.getOrElse(base + 1 + c) { 0 }
            var color = th.off
            if (bits and 1 != 0) color = th.text else if (bits and 2 != 0) color = th.text.copy(alpha = 0.45f)
            if (bits and 4 != 0) color = color.copy(alpha = color.alpha * 0.12f)
            val x = c * (w + gap)
            drawRoundRect(color, Offset(x, 0f), Size(w, size.height), rad)
            if (c == pos) {
                val i = 0.75.dp.toPx()
                drawRoundRect(th.accent, Offset(x + i, i), Size(w - 2 * i, size.height - 2 * i), rad, style = Stroke(1.5.dp.toPx()))
            }
        }
    }
}

@Composable
private fun PlusThumb(modifier: Modifier) {
    val th = LocalTheme.current
    Box(modifier, contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val w = 2.dp.toPx()
            drawRoundRect(th.line, Offset(w / 2, w / 2), Size(size.width - w, size.height - w), CornerRadius(6.dp.toPx()),
                style = Stroke(w, pathEffect = PathEffect.dashPathEffect(floatArrayOf(5.dp.toPx(), 4.dp.toPx()))))
        }
        Text("+", color = th.dim, fontSize = 20.isp())
    }
}
