package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import app.jambo.android.AppNative
import kotlin.math.round

private data class ArrangePoint(val step: Double, val value: Double)
private const val MAX_AUTO_POINTS = 2048

@Composable
internal fun ArrangeAutoPanel(
    s: ScreenState, data: ArrangeData, port: ArrangeViewport, header: Dp, height: Dp,
    modifier: Modifier, scroll: (Float) -> Unit = {},
) {
    val theme = LocalTheme.current
    val track = if (s.tracks[2] == 0) s.tracks[1] else -1
    val lanes = remember(track, s.revision) { AppNative.arrangeAutoParams(track, true).toList().chunked(2) }
    var chosen by remember { mutableStateOf("") }
    val lane = lanes.firstOrNull { it[0] == chosen } ?: lanes.firstOrNull()
    var adding by remember(track) { mutableStateOf(false) }
    var laneMenu by remember(track) { mutableStateOf<String?>(null) }
    var drawing by remember(track) { mutableStateOf(false) }
    var lastLaneTap by remember(track) { mutableStateOf(0L) }
    fun save(id: String, points: List<ArrangePoint>) {
        val out = DoubleArray(points.size * 2)
        points.forEachIndexed { i, p -> out[i * 2] = p.step; out[i * 2 + 1] = p.value }
        AppNative.arrangeSetAuto(track, id, out)
        s.refresh()
    }
    Row(modifier.height(height), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        Column(Modifier.width(header - Layout.gap.dp).fillMaxHeight().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(4.dp)) {
            for ((id, name) in lanes) {
                MenuItem(name, current = id == lane?.first()) {
                    val now = android.os.SystemClock.uptimeMillis()
                    if (chosen == id && now - lastLaneTap < 300) laneMenu = id
                    chosen = id
                    lastLaneTap = now
                }
            }
            if (track >= 0) {
                MenuItem("+") { adding = true }
                if (lanes.isNotEmpty()) MenuItem("CLEAR TRACK", red = true) {
                    AppNative.arrangeDeleteAuto(track, null)
                    s.refresh()
                }
            }
        }
        if (lane == null) {
            Box(Modifier.weight(1f).fillMaxHeight().tile(theme.panel), contentAlignment = Alignment.Center) {
                Text("AUTO", color = theme.dim, fontSize = 13.tsp())
            }
        } else {
            val id = lane[0]
            val points = remember(track, id, s.revision) {
                val raw = AppNative.arrangeAutoPoints(track, id)
                List(raw.size / 2) { ArrangePoint(raw[2 * it], raw[2 * it + 1]) }
            }
            var draft by remember(track, id) { mutableStateOf<List<ArrangePoint>?>(null) }
            val current by rememberUpdatedState(points)
            val currentPort by rememberUpdatedState(port)
            val currentData by rememberUpdatedState(data)
            val set by rememberUpdatedState<(List<ArrangePoint>) -> Unit> { save(id, it) }
            val currentScroll by rememberUpdatedState(scroll)
            Box(Modifier.weight(1f).fillMaxHeight().tile(theme.panel)) {
                Canvas(Modifier.fillMaxSize().clipToBounds().pointerInput(track, id, drawing) {
                    val taps = SampleDoubleTap()
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        val p = currentPort
                        val d = currentData
                        val width = p.stepWidth.takeIf { it > 0 } ?: size.width / 256f
                        val base = current
                        val initial = down.position
                        fun xy(point: ArrangePoint) = Offset((point.step * width - p.x).toFloat(),
                            (size.height * (1 - point.value)).toFloat())
                        fun point(at: Offset): ArrangePoint {
                            val grid = if (d.snap) d.gridSteps.toDouble() else 0.25
                            val x = (at.x + p.x) / width
                            return ArrangePoint((round(x / grid) * grid).coerceAtLeast(0.0),
                                (1.0 - at.y / size.height.coerceAtLeast(1)).coerceIn(0.0, 1.0))
                        }
                        val index = base.indices.filter { base[it].step.isFinite() }
                            .minByOrNull { (xy(base[it]) - initial).getDistance() }
                            ?.takeIf { (xy(base[it]) - initial).getDistance() <= 18.dp.toPx() } ?: -1
                        val chunk = sortedMapOf<Double, ArrangePoint>()
                        var dragged = false
                        try {
                            val up = trackSampleTouch(down, { it.getDistance() >= 8.dp.toPx() }) { pos, _ ->
                                dragged = true
                                val candidate = point(pos)
                                when {
                                    drawing -> {
                                        chunk[candidate.step] = candidate
                                        val range = chunk.firstKey()..chunk.lastKey()
                                        draft = base.filter { it.step !in range } + chunk.values
                                    }
                                    index >= 0 -> {
                                        val group = d.tracks.getOrNull(track)?.blocks?.firstOrNull {
                                            base[index].step >= it.start && base[index].step <= it.end
                                        }
                                        val min = base.subList(0, index).lastOrNull { it.step.isFinite() }?.step ?: 0.0
                                        val max = base.drop(index + 1).firstOrNull { it.step.isFinite() }?.step ?: 1_000_000.0
                                        val lo = maxOf(min, group?.start?.toDouble() ?: 0.0)
                                        val hi = minOf(max, group?.end?.toDouble() ?: 1_000_000.0)
                                        if (lo <= hi) draft = base.toMutableList().also {
                                            it[index] = candidate.copy(step = candidate.step.coerceIn(lo, hi))
                                        }
                                    }
                                    else -> currentScroll((p.x + initial.x - pos.x).coerceAtLeast(0f))
                                }
                            }
                            if (up != null) {
                                val edited = if (dragged) draft else if (index >= 0) {
                                    if (taps.accept(up, index, 24.dp.toPx())) base.filterIndexed { i, _ -> i != index } else null
                                } else {
                                    val next = point(up.position)
                                    base.filter { it.step != next.step } + next
                                }
                                if (edited != null) set(edited.filter { it.step.isFinite() }.take(MAX_AUTO_POINTS))
                            }
                        } finally {
                            draft = null
                        }
                    }
                }) {
                    val width = port.stepWidth.takeIf { it > 0 } ?: size.width / 256
                    fun x(step: Double) = (step * width - port.x).toFloat()
                    for (n in 0..4) drawLine(theme.line, Offset(0f, n * size.height / 4), Offset(size.width, n * size.height / 4))
                    val step = if (width * data.gridSteps < 5.dp.toPx()) 16 else data.gridSteps
                    val from = ((port.x / width).toInt() / step) * step
                    for (n in from..((port.x + size.width) / width).toInt() step step) {
                        val at = x(n.toDouble())
                        drawLine(if (n % 16 == 0) theme.dim else theme.line, Offset(at, 0f), Offset(at, size.height))
                    }
                    val curve = draft ?: points
                    val path = Path()
                    var active = false
                    for (point in curve) {
                        if (!point.step.isFinite()) {
                            active = false
                            continue
                        }
                        val y = (1 - point.value).toFloat() * size.height
                        val at = x(point.step)
                        if (active) path.lineTo(at, y) else path.moveTo(at, y)
                        active = true
                        drawCircle(theme.accent, 4.dp.toPx(), Offset(at, y))
                    }
                    drawPath(path, theme.accent, style = androidx.compose.ui.graphics.drawscope.Stroke(1.5.dp.toPx()))
                    val cursor = x(s.arrangePosition.toDouble())
                    drawLine(theme.accent, Offset(cursor, 0f), Offset(cursor, size.height))
                }
                InkButton("✎", drawing, Modifier.align(Alignment.TopEnd).width(44.dp)) { drawing = !drawing }
            }
        }
        Box(Modifier.width(Layout.meterWidth.dp))
    }
    if (adding) Dialog(onDismissRequest = { adding = false }) {
        val params = remember(track, s.revision) { AppNative.arrangeAutoParams(track, false).toList().chunked(2) }
        Column(Modifier.width(300.dp).fillMaxHeight(0.7f).menuBox().padding(8.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(4.dp)) {
            for ((id, title) in params) MenuItem(title, current = lanes.any { it[0] == id }) {
                if (lanes.none { it[0] == id }) save(id, emptyList())
                chosen = id
                adding = false
            }
        }
    }
    laneMenu?.let { id ->
        Dialog(onDismissRequest = { laneMenu = null }) {
            Column(Modifier.width(200.dp).menuBox().padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                MenuItem("CLEAR") { save(id, emptyList()); laneMenu = null }
                MenuItem("DELETE", red = true) {
                    AppNative.arrangeDeleteAuto(track, id)
                    s.refresh()
                    laneMenu = null
                }
            }
        }
    }
}
