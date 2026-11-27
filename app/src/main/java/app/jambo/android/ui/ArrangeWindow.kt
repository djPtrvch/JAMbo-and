package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
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
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.roundToInt

internal data class ArrangeViewport(val x: Float = 0f, val y: Float = 0f, val stepWidth: Float = 0f)

internal sealed interface ArrangeMenu {
    data object Grid : ArrangeMenu
    data object Group : ArrangeMenu
    data class Lane(val track: Int, val step: Int) : ArrangeMenu
    data class Block(val track: Int, val block: Int) : ArrangeMenu
    data class Track(val track: Int) : ArrangeMenu
    data class Ruler(val step: Int, val section: Int?) : ArrangeMenu
}

private data class TimelineGeometry(
    val header: Float, val ruler: Float, val sectionBand: Float, val row: Float, val gap: Float,
    val stepWidth: Float, val port: ArrangeViewport,
) {
    val bodyTop get() = ruler + sectionBand
    fun x(step: Float) = header + step * stepWidth - port.x
    fun step(x: Float) = (x - header + port.x) / stepWidth
    fun track(y: Float) = floor((y - bodyTop + port.y) / row).toInt()
    fun rowTop(track: Int) = bodyTop + track * row - port.y
}

@Composable
internal fun ArrangeWindow(s: ScreenState, width: Float, modifier: Modifier, steps: @Composable () -> Unit) {
    val d = remember(s.arrange) { ArrangeData(s.arrange) }
    val sectionNames = remember(s.revision) { AppNative.arrangeSections() }
    val theme = LocalTheme.current
    var port by remember { mutableStateOf(ArrangeViewport()) }
    var menu by remember { mutableStateOf<ArrangeMenu?>(null) }
    var group by remember { mutableStateOf<List<Pair<Int, Int>>>(emptyList()) }
    var marquee by remember { mutableStateOf<Rect?>(null) }
    var outline by remember { mutableStateOf<Rect?>(null) }
    var knobs by remember { mutableStateOf(false) }
    val measure = rememberTextMeasurer()
    val density = LocalDensity.current
    val header = column() + Layout.gap.dp
    val text = 13.tsp()
    fun groupAction(command: Int, delta: Int = 0) {
        val ids = group.flatMap { listOf(it.first, it.second) }.toIntArray()
        val updated = AppNative.arrangeGroup(command, delta, ids)
        group = updated.toList().chunked(2).map { (track, block) -> track to block }
        s.refresh()
    }

    Column(modifier, verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        Row(Modifier.fillMaxWidth().height(Layout.topHeight.dp), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            InkButton("▦", menu == ArrangeMenu.Grid, Modifier.weight(1f), Layout.topHeight) { menu = ArrangeMenu.Grid }
            Tool("TRIM", Modifier.weight(1f)) {
                if (group.size >= 2) groupAction(3) else s.arrangeAct(ArrangeAct.SPLIT)
            }
            Tool("⇤", Modifier.weight(1f)) { s.arrangeAct(ArrangeAct.SEEK, 0) }
            InkButton("LOOP", d.loop, Modifier.weight(1f), Layout.topHeight) { s.arrangeAct(ArrangeAct.LOOP) }
            InkButton("AUTO", d.auto, Modifier.weight(1f), Layout.topHeight) { s.arrangeAct(ArrangeAct.AUTO) }
            InkButton("KNOBS", knobs, Modifier.weight(1f), Layout.topHeight) { knobs = !knobs }
            InkButton("STEPS", !d.hideSteps, Modifier.weight(1f), Layout.topHeight) { s.arrangeAct(ArrangeAct.STEPS) }
        }
        val lower = (Layout.trackHeight + Layout.seqHeight + Layout.keySide(width, s.keys[1].coerceAtLeast(7)) +
            3 * Layout.gap).dp
        BoxWithConstraints(Modifier.weight(1f).fillMaxWidth()) {
            val timelineWidth = maxWidth - (Layout.meterWidth + Layout.gap).dp
            val geometry = with(density) {
                val headerPx = header.toPx()
                val ruler = 20.dp.toPx()
                val section = 18.dp.toPx()
                val gap = Layout.gap.dp.toPx()
                val st = port.stepWidth.takeIf { it > 0 } ?: ((timelineWidth.toPx() - headerPx) / 256).coerceAtLeast(0.1f)
                val hidden = if (d.auto || !d.hideSteps) lower.toPx() else 0f
                val row = ((maxHeight.toPx() + hidden - ruler - section + gap) / 8).coerceAtLeast(gap + 1)
                TimelineGeometry(headerPx, ruler, section, row, gap, st, port)
            }
            val geo by rememberUpdatedState(geometry)
            val data by rememberUpdatedState(d)
            val names = s.trackNames
            val take = remember(s.arrangePosition.toInt(), s.revision) { AppNative.arrangeTake() }
            Canvas(Modifier.width(timelineWidth).fillMaxHeight().clipToBounds().pointerInput(Unit) {
                var lastTap = -1_000L
                var lastPos = Offset.Zero
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val p = down.position
                    val g = geo
                    val initial = data
                    val t = if (p.y < g.bodyTop) -1 else g.track(p.y)
                    val row = initial.tracks.getOrNull(t)
                    val step = g.step(p.x)
                    val headerHit = p.x < g.header
                    val k = if (headerHit || row == null) -1 else row.blocks.indexOfFirst {
                        p.x >= g.x(it.start.toFloat()) - 6.dp.toPx() && p.x <= g.x(it.end.toFloat()) + 6.dp.toPx()
                    }
                    val b = row?.blocks?.getOrNull(k)
                    val selectedGroup = t to k in group
                    val edge = b?.let {
                        val reach = (it.length * g.stepWidth / 4).coerceIn(6.dp.toPx(), 14.dp.toPx())
                        when {
                            p.x <= g.x(it.start.toFloat()) + reach -> -1
                            p.x >= g.x(it.end.toFloat()) - reach -> 1
                            else -> 0
                        }
                    } ?: 0
                    var final = p
                    var moved = false
                    var zoomed = false
                    var lifting = false
                    var selecting = false
                    var horizontalScroll = true
                    var zoom = g.stepWidth
                    down.consume()
                    while (true) {
                        val event = awaitPointerEvent()
                        val c = event.changes.firstOrNull { it.id == down.id } ?: break
                        if (c.isConsumed) break
                        val fingers = event.changes.filter { it.pressed }
                        if (fingers.size > 1 && !headerHit) {
                            val center = event.calculateCentroid()
                            val anchor = geo.step(center.x)
                            zoom = (zoom * event.calculateZoom()).coerceIn(0.5.dp.toPx(), 72.dp.toPx())
                            port = port.copy(stepWidth = zoom, x = (anchor * zoom - center.x + g.header).coerceAtLeast(0f))
                            event.changes.forEach { it.consume() }
                            moved = true
                            zoomed = true
                            continue
                        }
                        if (!c.pressed) {
                            c.consume()
                            val stepAtEnd = g.step(final.x).roundToInt()
                            if (!zoomed && moved) {
                                when {
                                    t < 0 && !headerHit && p.y < g.ruler -> s.arrangeAct(ArrangeAct.LOOP_RANGE,
                                        (step / 16).roundToInt() * 16, (stepAtEnd / 16f).roundToInt() * 16)
                                    selecting -> {
                                        val selected = mutableListOf<Pair<Int, Int>>()
                                        val rect = marquee
                                        if (rect != null) {
                                            initial.tracks.forEachIndexed { track, lane ->
                                                val top = g.rowTop(track)
                                                lane.blocks.forEachIndexed { block, value ->
                                                    val hit = Rect(g.x(value.start.toFloat()), top, g.x(value.end.toFloat()), top + g.row - g.gap)
                                                    if (hit.overlaps(rect)) selected += track to block
                                                }
                                            }
                                        }
                                        group = selected.takeIf { it.size >= 2 } ?: emptyList()
                                        selected.singleOrNull()?.let { (track, block) -> s.arrangeAct(ArrangeAct.SELECT, track, block) }
                                    }
                                    b != null && edge < 0 -> s.arrangeAct(ArrangeAct.RESIZE, t, k, stepAtEnd, b.end)
                                    b != null && edge > 0 -> s.arrangeAct(ArrangeAct.RESIZE, t, k, b.start, stepAtEnd)
                                    b != null && lifting -> {
                                        val delta = (g.step(final.x) - step).roundToInt()
                                        if (selectedGroup) {
                                            val nativeStep = if (initial.snap) initial.gridSteps else 1
                                            groupAction(0, (delta.toFloat() / nativeStep).roundToInt() * nativeStep)
                                        } else s.arrangeAct(ArrangeAct.MOVE, t, k, b.start + delta)
                                    }
                                    headerHit && row != null && abs((final - p).y) >= 30.dp.toPx() ->
                                        s.act(Act.SWIPE, t, if (final.y > p.y) 1 else 0)
                                }
                            } else if (!moved) {
                                val double = c.uptimeMillis - lastTap < 320 && (p - lastPos).getDistance() < 30.dp.toPx()
                                lastTap = if (double) -1_000L else c.uptimeMillis
                                lastPos = p
                                if (t < 0 && !headerHit) {
                                    if (double) {
                                        val section = initial.sections.indexOfLast { it.start <= step }.takeIf { it >= 0 }
                                        menu = ArrangeMenu.Ruler(step.roundToInt(), section)
                                    } else s.arrangeAct(ArrangeAct.SEEK, step.roundToInt())
                                } else if (row != null) {
                                    when {
                                        headerHit -> {
                                            s.arrangeAct(ArrangeAct.SELECT, t, -1)
                                            if (double) menu = ArrangeMenu.Track(t)
                                        }
                                        b != null -> {
                                            if (!selectedGroup) {
                                                group = emptyList()
                                                s.arrangeAct(ArrangeAct.SELECT, t, k)
                                            }
                                            if (double) menu = if (selectedGroup) ArrangeMenu.Group else ArrangeMenu.Block(t, k)
                                        }
                                        else -> {
                                            group = emptyList()
                                            if (double) menu = ArrangeMenu.Lane(t, floor(step).toInt())
                                        }
                                    }
                                }
                            }
                            break
                        }
                        if (zoomed) continue
                        final = c.position
                        val delta = final - p
                        if (!moved && delta.getDistance() < 10.dp.toPx()) continue
                        if (!moved) {
                            lifting = c.uptimeMillis - down.uptimeMillis >= 350
                            selecting = lifting && b == null && t >= 0 && !headerHit
                            horizontalScroll = abs(delta.x) > abs(delta.y)
                            if (edge != 0) group = emptyList()
                        }
                        moved = true
                        c.consume()
                        when {
                            t < 0 && !headerHit && p.y >= g.ruler -> s.arrangeAct(ArrangeAct.SEEK, g.step(final.x).roundToInt())
                            selecting -> marquee = Rect(minOf(p.x, final.x), minOf(p.y, final.y), maxOf(p.x, final.x), maxOf(p.y, final.y))
                            b != null && (edge != 0 || lifting) -> {
                                val raw = g.step(final.x)
                                val at = if (initial.snap) (raw / initial.gridSteps).roundToInt() * initial.gridSteps else raw.roundToInt()
                                val deltaStep = ((raw - step) / initial.gridSteps).roundToInt() * initial.gridSteps
                                val left = when {
                                    edge < 0 -> minOf(at, b.end - 1)
                                    edge > 0 -> b.start
                                    else -> (b.start + deltaStep).coerceAtLeast(0)
                                }
                                val right = when {
                                    edge > 0 -> maxOf(at, b.start + 1)
                                    edge < 0 -> b.end
                                    else -> left + b.length
                                }
                                val top = g.rowTop(t)
                                outline = Rect(g.x(left.toFloat()), top, g.x(right.toFloat()), top + g.row - g.gap)
                            }
                            t < 0 || headerHit -> Unit
                            else -> {
                                val maxY = (initial.tracks.size * g.row - size.height + g.bodyTop).coerceAtLeast(0f)
                                port = if (horizontalScroll) g.port.copy(x = (g.port.x - delta.x).coerceAtLeast(0f))
                                    else g.port.copy(y = (g.port.y - delta.y).coerceIn(0f, maxY))
                            }
                        }
                    }
                    marquee = null
                    outline = null
                }
            }) {
                val g = geometry
                val end = g.step(size.width).coerceAtLeast(0f)
                drawRect(theme.panel)
                clipRect(left = g.header) {
                    if (d.loopEnd > d.loopStart) {
                        val lo = g.x(d.loopStart.toFloat())
                        val hi = g.x(d.loopEnd.toFloat())
                        drawRect(theme.accent.copy(alpha = if (d.loop) 0.75f else 0.35f), Offset(lo, 0f), Size(hi - lo, g.ruler))
                    }
                    val step = if (g.stepWidth * d.gridSteps < 5.dp.toPx()) 16 else d.gridSteps
                    val first = floor(g.step(g.header) / step).toInt().coerceAtLeast(0) * step
                    for (q in first..end.toInt() step step) {
                        val x = g.x(q.toFloat())
                        drawLine((if (q % 16 == 0) theme.dim else theme.line).copy(alpha = 0.65f),
                            Offset(x, 0f), Offset(x, size.height), (if (q % 64 == 0) 2.dp else 1.dp).toPx())
                        if (q % 16 == 0) drawText(measure.measure((q / 16 + 1).toString(),
                            TextStyle(color = theme.text, fontSize = text)), topLeft = Offset(x + 4.dp.toPx(), 0f))
                    }
                    d.sections.forEachIndexed { i, section ->
                        val x = g.x(section.start.toFloat())
                        val next = section.end.takeIf { it > section.start }
                            ?: d.sections.getOrNull(i + 1)?.start ?: maxOf(d.end, end.toInt() + 1)
                        val w = g.x(next.toFloat()) - x
                        drawLine(theme.accent, Offset(x, g.ruler), Offset(x, g.bodyTop))
                        if (section.end >= 0) drawLine(theme.accent, Offset(x + w, g.ruler), Offset(x + w, g.bodyTop))
                        val label = measure.measure(sectionNames.getOrElse(i) { "" }, TextStyle(color = theme.text, fontSize = text))
                        val labelX = if (section.end < 0 && i == d.sections.lastIndex) x + 4.dp.toPx()
                            else x + ((w - label.size.width) / 2).coerceAtLeast(4.dp.toPx())
                        drawText(label, topLeft = Offset(labelX, g.ruler))
                    }
                    clipRect(top = g.bodyTop) {
                        d.tracks.forEachIndexed { t, lane ->
                            val y = g.rowTop(t)
                            if (y + g.row <= g.bodyTop || y >= size.height) return@forEachIndexed
                            drawRect(theme.bg, Offset(g.header, y + g.row - g.gap), Size(size.width - g.header, g.gap))
                            lane.blocks.forEachIndexed blockLoop@{ k, block ->
                                val x = g.x(block.start.toFloat())
                                val w = block.length * g.stepWidth
                                if (x + w < g.header || x > size.width) return@blockLoop
                                val selected = t to k in group || group.isEmpty() && t == d.selectedTrack && k == d.selectedBlock
                                val h = g.row - g.gap
                                val radius = CornerRadius(4.dp.toPx())
                                val opacity = if (lane.mute) 0.35f else 1f
                                drawRoundRect(arrangeColor(lane.color).copy(alpha = opacity), Offset(x, y), Size(w, h), radius)
                                if (selected && w > 2.dp.toPx()) drawRoundRect(theme.text, Offset(x + 1.dp.toPx(), y + 1.dp.toPx()),
                                    Size(w - 2.dp.toPx(), h - 2.dp.toPx()), radius, style = Stroke(2.dp.toPx()))
                                val side = (g.stepWidth * 0.7f).coerceAtMost(h * 0.4f).coerceAtLeast(2.dp.toPx())
                                val pad = ((g.stepWidth - side) / 2).coerceAtLeast(0.5f)
                                val start = maxOf(block.start, g.step(g.header).toInt() - 16)
                                val stop = minOf(block.end, end.toInt() + 1)
                                clipRect(left = maxOf(x, g.header), right = minOf(x + w, size.width), top = y, bottom = y + h) {
                                    for (stepAt in start until stop) {
                                        val length = block.triggerAt(stepAt)
                                        if (length <= 0) continue
                                        val markWidth = if (length == 1) side else (length * g.stepWidth - 2 * pad).coerceAtLeast(side)
                                        drawRect(Color(0xff111111).copy(alpha = 0.6f), Offset(g.x(stepAt.toFloat()) + pad, y + (h - side) / 2),
                                            Size(markWidth, side))
                                    }
                                }
                            }
                        }
                        marquee?.let {
                            val start = Offset(it.left, it.top)
                            val area = Size(it.width, it.height)
                            drawRect(theme.text.copy(alpha = 0.15f), start, area)
                            drawRect(theme.text, start, area, style = Stroke(1.dp.toPx()))
                        }
                        outline?.let {
                            drawRect(theme.accent, Offset(it.left, it.top), Size(it.width, it.height),
                                style = Stroke(2.dp.toPx(), pathEffect = androidx.compose.ui.graphics.PathEffect.dashPathEffect(
                                    floatArrayOf(4.dp.toPx(), 4.dp.toPx()))))
                        }
                        for (i in take.indices step 3) {
                            val x = g.x(take[i + 1].toFloat())
                            val span = (take[i + 2] - take[i + 1]) * g.stepWidth
                            if (span > 0) drawRect(theme.rec.copy(alpha = 0.55f), Offset(x, g.rowTop(take[i])),
                                Size(span, g.row - g.gap))
                        }
                    }
                    val cursor = g.x(s.arrangePosition.toFloat())
                    drawLine(theme.accent, Offset(cursor, g.bodyTop), Offset(cursor, size.height), 2.dp.toPx())
                    drawMarkerFlag(theme.accent, cursor - 8.dp.toPx(), 16.dp.toPx(), 0f, g.bodyTop - 6.dp.toPx(),
                        Offset(cursor, g.bodyTop))
                }
                drawRect(theme.bg, size = Size(g.header, size.height))
                val bpm = (s.top[0] / 10.0).coerceAtLeast(20.0)
                val seconds = (s.arrangePosition * 15 / bpm).toInt()
                val remaining = ((d.end - s.arrangePosition).coerceAtLeast(0) * 15 / bpm).toInt()
                val timing = "%d:%02d / −%d:%02d".format(seconds / 60, seconds % 60, remaining / 60, remaining % 60)
                drawText(measure.measure(timing, TextStyle(color = theme.text, fontSize = text)), topLeft = Offset(8.dp.toPx(), 8.dp.toPx()))
                clipRect(right = g.header, top = g.bodyTop) {
                    d.tracks.forEachIndexed { t, lane ->
                        val y = g.rowTop(t)
                        val selected = s.tracks[2] == 0 && s.tracks[1] == t
                        drawRect(if (selected) theme.panel2 else theme.panel, Offset(0f, y),
                            Size(g.header - g.gap, g.row - g.gap))
                        drawRect(arrangeColor(lane.color), Offset(0f, y), Size(5.dp.toPx(), g.row - g.gap))
                        val state = if (lane.mute) "  M" else if (lane.solo) "  S" else ""
                        drawText(measure.measure(names.getOrElse(t) { (t + 1).toString() } + state,
                            TextStyle(color = if (selected) theme.accent else theme.text, fontSize = text)),
                            topLeft = Offset(12.dp.toPx(), y + 8.dp.toPx()))
                    }
                }
            }
            Box(Modifier.align(Alignment.CenterEnd).fillMaxHeight().width(Layout.meterWidth.dp)) { MeterColumn(s, Modifier.fillMaxHeight()) }
            if (knobs) {
                Row(Modifier.width(timelineWidth).fillMaxHeight().tile(theme.bg),
                    horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                    LeftColumn(s, Modifier.width(column()).fillMaxHeight())
                    KnobGrid(s, Modifier.weight(1f).fillMaxHeight())
                    RightColumn(s, Modifier.width(rightColumn()).fillMaxHeight()) {}
                }
            }
        }
        if (d.auto) ArrangeAutoPanel(s, d, port, header, lower, Modifier.fillMaxWidth()) {
            port = port.copy(x = it)
        }
        else if (!d.hideSteps) steps()
    }
    menu?.let { ArrangeMenus(s, d, it, sectionNames, { command -> groupAction(command) }) { menu = null } }
}
