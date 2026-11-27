package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
    val currentSetView by rememberUpdatedState(setView)
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
        val taps = SampleDoubleTap()
        awaitEachGesture {
            val down = awaitFirstDown()
            val p = down.position
            val m = current
            val view0 = currentView
            val first = m.firstBeat
            val range = m.endBeat - first
            val nav = SampleNavigation(view0, p, size.width, m.frames, 110.dp.toPx())
            val beat = first + view0.fraction(p.x, size.width) * range
            val atTop = p.y < top.toPx()
            val atBottom = p.y >= size.height - (bottom + 8.dp).toPx()
            val reach = if (atTop || atBottom) flagWidth.toPx() / 2 + 6.dp.toPx() else 14.dp.toPx()
            val hit = nearestWarp(m, beat, hitTolerance(view0, m, size.width, reach), atBottom, atTop)
            var index = if (hit == -1 && atBottom && selected in m.markers.indices) selected else hit
            val topDrag = atTop && index >= 0
            val originalBeat = when (index) {
                startMark -> m.beatOf(0.0)
                -1 -> beat
                else -> m.markers[index].beat
            }
            if (index != -1) frozen = m
            if (index >= 0) selected = index
            var moved = false
            var editingBeat = false
            var targetBeat = originalBeat
            try {
                val up = trackSampleTouch(down, {
                    (if (topDrag) abs(it.x) else it.getDistance()) >= 10.dp.toPx()
                }) { position, firstMove ->
                    moved = true
                    if (index == -1) {
                        nav.move(position)?.let(currentSetView)
                    } else {
                        if (firstMove && !topDrag) {
                            s.act(Act.BEGIN_GESTURE)
                            editingBeat = true
                            if (index == startMark) {
                                s.warp(WarpAct.START)
                                index = 0
                                selected = index
                            }
                        }
                        targetBeat = originalBeat + (position.x - p.x) / size.width.coerceAtLeast(1) *
                            view0.span * range
                        if (topDrag) {
                            movingTop = index to view0.x((targetBeat - first) / range, size.width.toFloat())
                        } else {
                            s.warp(WarpAct.MOVE, index, b = snapWarpBeat(targetBeat, currentGrid))
                        }
                    }
                }
                if (up != null && topDrag && moved) {
                    s.warp(WarpAct.PLACE, index, m.sourceAt(targetBeat), snapWarpBeat(targetBeat, currentGrid))
                } else if (up != null && !topDrag && !moved && taps.accept(up, hit, 24.dp.toPx())) {
                    if (hit >= 0) {
                        s.warp(WarpAct.REMOVE, hit)
                        selected = -1
                    } else if (hit != startMark) {
                        val at = m.sourceAt(first + view0.fraction(up.position.x, size.width) * range)
                        if (at > 0 && at < 1) s.warp(WarpAct.ADD, a = at)
                        selected = -1
                    }
                }
            } finally {
                if (editingBeat) s.act(Act.END_GESTURE)
                frozen = null
                movingTop = null
            }
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
        val pxPerBeat = size.width / (end - origin).coerceAtLeast(1e-9)
        var step = grid
        while (step * pxPerBeat < 8.dp.toPx() && step < 1e8) step *= 2
        var tick = floor(origin / step) * step
        while (tick <= end) {
            val bar = tick % 4.0 == 0.0
            val beat = tick % 1.0 == 0.0
            val at = x(tick)
            if (at in 0f..size.width) {
                val color = theme.text.copy(alpha = if (bar) 0.85f else if (beat) 0.65f else 0.48f)
                val thickness = (if (bar) 2f else 1f) * 1.dp.toPx().coerceAtLeast(1f)
                drawRect(color, Offset(at.roundToInt().toFloat(), topPx),
                    Size(thickness, (size.height - bottomPx - topPx).coerceAtLeast(0f)))
                if (bar && 4 * pxPerBeat > 24.dp.toPx()) {
                    val label = measure.measure((tick / 4).toInt().plus(1).toString(),
                        TextStyle(color = theme.text, fontSize = if (phone) 14.sp else 12.sp, fontWeight = FontWeight.SemiBold))
                    drawText(label, topLeft = Offset(at + 8.dp.toPx(), (topPx - label.size.height) / 2))
                }
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
        drawSampleScrollbar(view, theme.panel2, theme.accent)
    }
}

private fun snapWarpBeat(beat: Double, step: Double): Double =
    if (beat < 0) ceil(beat / step - 0.5) * step else floor(beat / step + 0.5) * step

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
    drawMarkerFlag(color, x - half, width, y + h, y, Offset(x, y - 8.dp.toPx()))
    val layout = text.measure(label, TextStyle(color = ink, fontSize = 11.sp))
    drawText(layout, topLeft = Offset(x - layout.size.width / 2f, y + (h - layout.size.height) / 2))
    if (upper) {
        drawMarkerFlag(color, x - half, width, 4.dp.toPx(), top - 10.dp.toPx(), Offset(x, top - 2.dp.toPx()))
    }
}
