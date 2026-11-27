package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.pow
import kotlin.math.round
import kotlin.math.roundToInt

internal class WarpSample(data: DoubleArray) {
    val bpm = data[0]
    val off = data[2] != 0.0
    val follows = data[3] != 0.0
    val frames = data[4].toInt()
    val markers = (5 until data.lastIndex step 2).map { WarpPoint(data[it], data[it + 1]) }
    private val beatsPerSample = frames * bpm / (48_000 * 60)
    val firstBeat = floor(minOf(0.0, beatOf(0.0)))
    val endBeat = maxOf(firstBeat + 1, ceil(beatOf(1.0) + 1))
    val syntheticStart = markers.firstOrNull()?.let { it.at * frames >= 48 } ?: true

    fun beatOf(at: Double): Double = interpolate(at, true)
    fun sourceAt(beat: Double): Double = interpolate(beat, false)
    private fun interpolate(v: Double, toBeat: Boolean): Double {
        val dx = if (toBeat) beatsPerSample else 1 / beatsPerSample.coerceAtLeast(1e-9)
        if (markers.isEmpty()) return v * dx
        fun x(m: WarpPoint) = if (toBeat) m.at else m.beat
        fun y(m: WarpPoint) = if (toBeat) m.beat else m.at
        val index = markers.indexOfFirst { x(it) >= v }
        if (index <= 0) {
            val m = if (index == 0) markers.first() else markers.last()
            return y(m) + (v - x(m)) * dx
        }
        val a = markers[index - 1]
        val b = markers[index]
        return y(a) + (v - x(a)) / (x(b) - x(a)).coerceAtLeast(1e-12) * (y(b) - y(a))
    }
}

internal data class WarpPoint(val at: Double, val beat: Double)
internal data class SampleView(val left: Double = 0.0, val span: Double = 1.0) {
    val right get() = left + span
    fun fraction(x: Float, width: Int) = left + span * x / width.coerceAtLeast(1)
    fun x(fraction: Double, width: Float) = ((fraction - left) / span * width).toFloat()
}

internal fun editView(
    initial: SampleView, dx: Float, dy: Float, x: Float, width: Int, vertical: Boolean, frames: Int, zoomPx: Float,
): SampleView {
    val fraction = (x / width.coerceAtLeast(1)).coerceIn(0f, 1f)
    val span = if (vertical) (initial.span * 2.0.pow((dy / zoomPx).toDouble())).coerceIn(
        minOf(1.0, 256.0 / frames.coerceAtLeast(1)), 1.0,
    ) else initial.span
    val left = if (vertical) initial.left + (initial.span - span) * fraction
        else initial.left - dx / width.coerceAtLeast(1) * span
    return SampleView(left.coerceIn(0.0, 1 - span), span)
}

internal object WarpAct {
    const val ENTER = 0
    const val BYPASS = 1
    const val BPM = 2
    const val AUTO = 3
    const val CLEAR = 4
    const val ADD = 5
    const val START = 6
    const val MOVE = 7
    const val PLACE = 8
    const val REMOVE = 9
}

internal fun ScreenState.warp(command: Int, index: Int = -1, a: Double = 0.0, b: Double = 0.0) {
    AppNative.sampleWarpAct(command, index, a, b)
    refresh()
}

private fun beatLabel(beat: Double): String {
    val ticks = (beat * 4).roundToInt()
    val bar = Math.floorDiv(ticks, 16) + 1
    val beatInBar = Math.floorMod(ticks, 16)
    val base = "$bar.${beatInBar / 4 + 1}"
    return if (beatInBar % 4 == 0) base else "$base.${beatInBar % 4 + 1}"
}

@Composable
internal fun RowScope.WarpTools(s: ScreenState, warp: WarpSample, grid: Double, setGrid: (Double) -> Unit, enabled: Boolean) {
    var input by remember { mutableStateOf(false) }
    val current by rememberUpdatedState(warp)
    val canSetBpm = enabled && !warp.follows
    val bpmModifier = if (canSetBpm) Modifier.pointerInput(Unit) {
        detectTapGestures(onDoubleTap = { input = true })
    }.pointerInput(Unit) {
        var base = 0.0
        var dy = 0f
        var begun = false
        try {
            detectDragGestures(
                onDragStart = {
                    base = current.bpm
                    dy = 0f
                    AppNative.act(Act.BEGIN_GESTURE)
                    begun = true
                },
                onDragEnd = { s.act(Act.END_GESTURE); begun = false },
                onDragCancel = { s.act(Act.END_GESTURE); begun = false },
            ) { change, amount ->
                change.consume()
                dy += amount.y
                s.warp(WarpAct.BPM, a = round(base - dy / 4.dp.toPx()).coerceIn(20.0, 400.0))
            }
        } finally {
            if (begun) s.act(Act.END_GESTURE)
        }
    } else Modifier
    val theme = LocalTheme.current
    androidx.compose.foundation.layout.Box(
        bpmModifier.weight(1.5f).fillMaxSize().tile(theme.panel2),
        contentAlignment = androidx.compose.ui.Alignment.Center,
    ) {
        Text("FILE BPM\n${"%.1f".format(java.util.Locale.US, warp.bpm)}", color = if (canSetBpm) theme.text else theme.dim,
            fontSize = 12.tsp(), textAlign = androidx.compose.ui.text.style.TextAlign.Center)
    }
    listOf("1/16" to 0.25, "1/8" to 0.5, "1/4" to 1.0).forEach { (label, step) ->
        if (enabled) InkButton(label, step == grid, Modifier.weight(0.8f), Layout.topHeight) { setGrid(step) }
        else Tool(label, Modifier.weight(0.8f), false) {}
    }
    Tool("AUTO WARP", Modifier.weight(1.5f), enabled) { s.warp(WarpAct.AUTO, a = grid) }
    Tool("CLEAR WARP", Modifier.weight(1.5f), enabled && warp.markers.isNotEmpty()) { s.warp(WarpAct.CLEAR) }
    if (input) InputDialog("FILE BPM", warp.bpm.toString(), 16, { input = false },
        extra = "AUTO" to { input = false; s.warp(WarpAct.BPM) }) { text ->
        val bpm = text.replace(',', '.').toDoubleOrNull()
        if (bpm != null && bpm.isFinite() && bpm > 0) {
            input = false
            s.warp(WarpAct.BPM, a = bpm)
        }
    }
}

private const val startMark = -2

@Composable
internal fun WarpWave(
    s: ScreenState, key: String, warp: WarpSample, grid: Double, channels: Int, columns: Int,
    view: SampleView, setView: (SampleView) -> Unit,
) {
    var selected by remember(key) { mutableIntStateOf(-1) }
    var frozen by remember(key) { mutableStateOf<WarpSample?>(null) }
    var movingTop by remember(key) { mutableStateOf<Pair<Int, Float>?>(null) }
    val current by rememberUpdatedState(warp)
    val currentView by rememberUpdatedState(view)
    val currentGrid by rememberUpdatedState(grid)
    val transform = frozen ?: warp
    val origin = transform.firstBeat + view.left * (transform.endBeat - transform.firstBeat)
    val end = transform.firstBeat + view.right * (transform.endBeat - transform.firstBeat)
    val peaks = remember(key, warp, origin, end) { AppNative.sampleWarpWave(columns, origin, end) }
    val theme = LocalTheme.current
    val measure = rememberTextMeasurer()
    val phone = LocalConfiguration.current.smallestScreenWidthDp < 600
    val flagWidth = if (phone) 60.dp else 36.dp
    val top = if (phone) 51.dp else 37.dp
    val bottom = if (phone) 46.dp else 30.dp

    Canvas(Modifier.fillMaxSize().pointerInput(key, phone) {
        detectTapGestures(
            onTap = { p ->
                val m = current
                val at = currentView.fraction(p.x, size.width)
                val beat = m.firstBeat + at * (m.endBeat - m.firstBeat)
                val hit = nearestWarp(m, beat, hitTolerance(currentView, m, size.width,
                    if (p.y >= size.height - bottom.toPx()) flagWidth.toPx() / 2 + 6.dp.toPx() else 14.dp.toPx()))
                if (hit >= 0) selected = hit
            },
            onDoubleTap = { p ->
                val m = current
                val fraction = currentView.fraction(p.x, size.width)
                val beat = m.firstBeat + fraction * (m.endBeat - m.firstBeat)
                val tolerance = hitTolerance(currentView, m, size.width,
                    if (p.y >= size.height - bottom.toPx()) flagWidth.toPx() / 2 + 6.dp.toPx() else 14.dp.toPx())
                val hit = nearestWarp(m, beat, tolerance, p.y >= size.height - bottom.toPx())
                if (hit >= 0) {
                    s.warp(WarpAct.REMOVE, hit)
                    selected = -1
                } else if (hit != startMark) {
                    val at = m.sourceAt(beat)
                    if (at > 0 && at < 1) s.warp(WarpAct.ADD, a = at)
                    selected = -1
                }
            },
        )
    }.pointerInput(key, phone) {
        var index = -1
        var topDrag = false
        var draggingBeat = false
        var position = Offset.Zero
        var anchor = Offset.Zero
        var view0 = SampleView()
        var vertical: Boolean? = null
        var originalBeat = 0.0
        var m0 = current
        fun close() {
            if (draggingBeat) s.act(Act.END_GESTURE)
            draggingBeat = false
            frozen = null
            movingTop = null
        }
        try {
            detectDragGestures(
                onDragStart = { p ->
                    m0 = current
                    view0 = currentView
                    anchor = p
                    position = p
                    vertical = null
                    val beat = m0.firstBeat + view0.fraction(p.x, size.width) * (m0.endBeat - m0.firstBeat)
                    val atTop = p.y < top.toPx()
                    val atBottom = p.y >= size.height - bottom.toPx() - 8.dp.toPx()
                    val reach = if (atTop || atBottom) flagWidth.toPx() / 2 + 6.dp.toPx() else 14.dp.toPx()
                    index = nearestWarp(m0, beat, hitTolerance(view0, m0, size.width, reach), atBottom, atTop)
                    if (index == -1 && atBottom && selected in m0.markers.indices) index = selected
                    topDrag = atTop && index >= 0
                    if (index != -1) {
                        frozen = m0
                        originalBeat = if (index == startMark) m0.beatOf(0.0) else m0.markers[index].beat
                        if (!topDrag) {
                            s.act(Act.BEGIN_GESTURE)
                            draggingBeat = true
                            if (index == startMark) {
                                s.warp(WarpAct.START)
                                index = 0
                            }
                        }
                        selected = index
                    }
                },
                onDragEnd = {
                    if (topDrag && index >= 0 && movingTop != null) {
                        val beat = originalBeat + (position.x - anchor.x) / size.width.coerceAtLeast(1) *
                            view0.span * (m0.endBeat - m0.firstBeat)
                        s.warp(WarpAct.PLACE, index, m0.sourceAt(beat), round(beat / currentGrid) * currentGrid)
                    }
                    close()
                },
                onDragCancel = { close() },
            ) { change, _ ->
                position = change.position
                change.consume()
                if (index >= 0) {
                    val beat = originalBeat + (position.x - anchor.x) / size.width.coerceAtLeast(1) *
                        view0.span * (m0.endBeat - m0.firstBeat)
                    if (topDrag) {
                        val f = (beat - m0.firstBeat) / (m0.endBeat - m0.firstBeat)
                        movingTop = index to view0.x(f, size.width.toFloat())
                    } else s.warp(WarpAct.MOVE, index, b = round(beat / currentGrid) * currentGrid)
                } else {
                    if (vertical == null) vertical = abs(position.y - anchor.y) > abs(position.x - anchor.x)
                    setView(editView(view0, position.x - anchor.x, position.y - anchor.y, anchor.x,
                        size.width, vertical == true, m0.frames, 110.dp.toPx()))
                }
            }
        } finally {
            close()
        }
    }) {
        fun x(beat: Double) = ((beat - origin) / (end - origin) * size.width).toFloat()
        val topPx = top.toPx()
        val bottomPx = bottom.toPx()
        drawRect(theme.bg.copy(alpha = 0.35f), Offset(0f, size.height - bottomPx), Size(size.width, bottomPx))
        val lane = (size.height - topPx - bottomPx).coerceAtLeast(1f) / channels.coerceAtLeast(1)
        if (peaks.size >= channels * columns * 2) {
            for (c in 0 until channels) for (k in 0 until columns) {
                val j = 2 * (c * columns + k)
                val y = topPx + (c + 0.5f) * lane
                val xx = size.width * (k + 0.5f) / columns
                drawLine(theme.text.copy(alpha = 0.64f), Offset(xx, y - peaks[j + 1] * lane * 0.45f), Offset(xx, y - peaks[j] * lane * 0.45f))
            }
        }
        var step = grid
        while (step * size.width / (end - origin) < 8.dp.toPx() && step < 1e8) step *= 2
        var tick = floor(origin / step) * step
        while (tick <= end) {
            val bar = abs(tick / 4 - round(tick / 4)) < 1e-6
            val beat = abs(tick - round(tick)) < 1e-6
            val color = theme.text.copy(alpha = if (bar) 0.78f else if (beat) 0.54f else 0.36f)
            val thickness = (if (bar) 2.2.dp else if (beat) 1.6.dp else 1.2.dp).toPx().coerceAtLeast(1f)
            val at = x(tick)
            if (at in 0f..size.width) drawLine(color, Offset(at, topPx), Offset(at, size.height - bottomPx), thickness)
            if (bar) {
                val label = measure.measure((round(tick / 4).toInt() + 1).toString(), TextStyle(color = theme.text, fontSize = if (phone) 14.sp else 12.sp, fontWeight = FontWeight.SemiBold))
                drawText(label, topLeft = Offset(x(tick) + 5.dp.toPx(), 4.dp.toPx()))
            }
            tick += step
        }
        for (k in 0 until s.heads.size - 1 step 2) {
            val at = x(warp.beatOf(s.heads[k].toDouble()))
            drawLine(theme.text.copy(alpha = s.heads[k + 1].coerceIn(0f, 1f)), Offset(at, topPx),
                Offset(at, size.height - bottomPx), 1.dp.toPx())
        }
        if (warp.syntheticStart) warpFlag(x(warp.beatOf(0.0)), "START", theme.dim, theme.bg, measure, flagWidth.toPx(),
            bottomPx, topPx, false)
        for ((i, mark) in warp.markers.withIndex()) {
            val at = movingTop?.takeIf { it.first == i }?.second ?: x(mark.beat)
            val color = if (warp.off) theme.dim else if (selected == i) theme.accent else theme.text
            warpFlag(at, beatLabel(mark.beat), color, theme.bg, measure, flagWidth.toPx(), bottomPx, topPx,
                mark.at * warp.frames >= 48)
        }
    }
}

private fun hitTolerance(view: SampleView, m: WarpSample, width: Int, pixels: Float) =
    pixels / width.coerceAtLeast(1) * view.span * (m.endBeat - m.firstBeat)

private fun nearestWarp(m: WarpSample, beat: Double, tolerance: Double, allowStart: Boolean = false, top: Boolean = false): Int {
    val candidates = m.markers.indices.filter { !top || m.markers[it].at * m.frames >= 48 }
    val i = candidates.minByOrNull { abs(m.markers[it].beat - beat) }
    if (i != null && abs(m.markers[i].beat - beat) <= tolerance) return i
    return if (allowStart && m.syntheticStart && abs(m.beatOf(0.0) - beat) <= tolerance) startMark else -1
}

private fun DrawScope.warpFlag(
    x: Float, label: String, color: Color, ink: Color, text: TextMeasurer, width: Float,
    strip: Float, top: Float, upper: Boolean,
) {
    if (x < -width / 2 || x > size.width + width / 2) return
    val half = width / 2
    val y = size.height - strip + 6.dp.toPx()
    val h = strip - 10.dp.toPx()
    drawLine(color, Offset(x, top), Offset(x, y), 1.dp.toPx())
    drawRect(color, Offset(x - half, y), Size(width, h))
    drawPath(Path().apply {
        moveTo(x - half, y); lineTo(x, y - 8.dp.toPx()); lineTo(x + half, y); close()
    }, color)
    val layout = text.measure(label, TextStyle(color = ink, fontSize = 11.sp))
    drawText(layout, topLeft = Offset(x - layout.size.width / 2f, y + (h - layout.size.height) / 2))
    if (upper) {
        val bar = 4.dp.toPx()
        drawPath(Path().apply {
            moveTo(x - half, bar); lineTo(x + half, bar); lineTo(x + half, top - 10.dp.toPx())
            lineTo(x, top - 2.dp.toPx()); lineTo(x - half, top - 10.dp.toPx()); close()
        }, color)
    }
}
