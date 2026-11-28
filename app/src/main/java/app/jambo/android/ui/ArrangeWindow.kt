package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.withFrameNanos
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.roundToInt

private data class CarryTarget(val step: Int, val insert: Boolean, val outline: Rect)

internal sealed interface ArrangeMenu {
    data object Grid : ArrangeMenu
    data object Group : ArrangeMenu
    data object Reference : ArrangeMenu
    data class Drop(val selection: IntArray, val step: Int) : ArrangeMenu
    data class Lane(val track: Int, val step: Int) : ArrangeMenu
    data class Block(val track: Int, val block: Int) : ArrangeMenu
    data class Track(val track: Int) : ArrangeMenu
    data class Ruler(val step: Int, val section: Int?) : ArrangeMenu
}

@Composable
internal fun ArrangeWindow(
    s: ScreenState, modifier: Modifier, steps: @Composable () -> Unit, knobsPanel: @Composable (Modifier) -> Unit,
) {
    val d = remember(s.arrange) { ArrangeData(s.arrange) }
    val sectionNames = remember(s.revision) { AppNative.arrangeSections() }
    val reference = arrangeReference(s, d.ref)
    val bpm = (s.top[0] / 10.0).coerceAtLeast(20.0)
    val stepSeconds = 15.0 / bpm
    var refDrag by remember { mutableStateOf<Double?>(null) }
    var timelineFrame by remember { mutableStateOf(Rect.Zero) }
    val theme = LocalTheme.current
    var menu by remember { mutableStateOf<ArrangeMenu?>(null) }
    var group by remember { mutableStateOf<List<Pair<Int, Int>>>(emptyList()) }
    var marquee by remember { mutableStateOf<Rect?>(null) }
    var outline by remember { mutableStateOf<Rect?>(null) }
    val measure = rememberTextMeasurer()
    val density = LocalDensity.current
    val header = column() + Layout.gap.dp
    val preferences = s.arrangePreferences
    val scaleTracks = preferences.scaleTracks
    val knobs = s.arrangeKnobs && s.arrangeCarry == null
    val text = 13.tsp()
    val meters = remember { mutableStateOf(ArrangeHeaderMeters(AppNative.arrangeHeaderMeters())) }
    LaunchedEffect(knobs) {
        if (!knobs) while (true) withFrameNanos { meters.value = ArrangeHeaderMeters(AppNative.arrangeHeaderMeters()) }
    }
    fun groupAction(command: Int, delta: Int = 0) {
        val ids = group.flatMap { listOf(it.first, it.second) }.toIntArray()
        val updated = AppNative.arrangeGroup(command, delta, ids)
        group = updated.toList().chunked(2).map { (track, block) -> track to block }
        s.refresh()
    }

    BoxWithConstraints(modifier) {
        val gap = Layout.gap.dp
        val toolbar = Layout.topHeight.dp
        val lower = Layout.lowerPanelHeight(s.keys.getOrElse(1) { 15 }).dp
        val lowerVisible = d.auto || !d.hideSteps
        val timelineWidth = maxWidth - (Layout.meterWidth + Layout.gap).dp
        val centerHeight = (maxHeight - lower - gap).coerceAtLeast(0.dp)
        val fullTimelineHeight = (maxHeight - toolbar - gap).coerceAtLeast(0.dp)
        val visibleHeight = (if (lowerVisible) fullTimelineHeight - lower - gap else fullTimelineHeight).coerceAtLeast(0.dp)
        val playing = s.top[3] != 0
        val playEnd = if (playing) (s.arrangePosition.toInt() / 256 + 1) * 256 else 0
        val geometry = with(density) {
            val headerPx = header.toPx()
            TimelineGeometry(
                headerPx, 20.dp.toPx(), 18.dp.toPx(), gap.toPx(),
                (timelineWidth.toPx() - headerPx).coerceAtLeast(1f), fullTimelineHeight.toPx(), visibleHeight.toPx(),
                d.tracks.size, maxOf(d.end, playEnd), reference != null, scaleTracks || !preferences.compact,
                this.density, s.arrangeViewport,
            )
        }
        val follow = preferences.follow && playing && s.arrangeCarry == null
        LaunchedEffect(follow, s.arrangePosition, geometry.width, geometry.stepWidth) {
            if (!follow) return@LaunchedEffect
            val at = s.arrangePosition.toFloat() * geometry.stepWidth
            val x = geometry.port.x
            val next = when {
                at - x > geometry.width * 0.75f -> at - geometry.width * 0.75f
                at < x -> at - geometry.width * 0.10f
                else -> return@LaunchedEffect
            }
            s.arrangeViewport = geometry.port.copy(x = geometry.clampX(next))
        }
        val geo by rememberUpdatedState(geometry)
        val resizingTracks by rememberUpdatedState(scaleTracks)
        val data by rememberUpdatedState(d)
        val ref by rememberUpdatedState(reference)
        val stepDuration by rememberUpdatedState(stepSeconds)
        val names = s.trackNames
        val take = remember(s.arrangePosition.toInt(), s.revision) { AppNative.arrangeTake() }
        val carry = s.arrangeCarry
        val target = carry?.let { move ->
            val local = move.point - timelineFrame.topLeft
            val lane = d.tracks.getOrNull(move.track)
            if (move.selection != null || lane == null || local.y < geometry.rulerBottom ||
                local.y > timelineFrame.height || local.x < geometry.header || local.x > timelineFrame.width) null
            else with(density) {
                val near = lane.blocks.firstOrNull { abs(geometry.x(it.start.toFloat()) - local.x) < 16.dp.toPx() }
                val step = floor(geometry.step(local.x)).toInt().coerceAtLeast(0)
                val at = near?.start ?: if (d.snap) (step + d.gridSteps / 2) / d.gridSteps * d.gridSteps else step
                val y = geometry.rowTop(move.track)
                CarryTarget(at, near != null, Rect(geometry.x(at.toFloat()), y,
                    geometry.x((at + move.length).toFloat()), y + geometry.row - geometry.gap))
            }
        }
        val selection = carry?.selection
        val selectionStep = if (selection != null && carry != null) {
            val local = carry.point - timelineFrame.topLeft
            if (local.y in geometry.rulerBottom..timelineFrame.height &&
                local.x in geometry.header..timelineFrame.width) {
                val step = floor(geometry.step(local.x)).toInt().coerceAtLeast(0)
                if (d.snap) (step + d.gridSteps / 2) / d.gridSteps * d.gridSteps else step
            } else null
        } else null
        val preview = remember(selection, selectionStep, s.revision) {
            if (selection != null && selectionStep != null) AppNative.arrangeSelectionPreview(selection, selectionStep)
            else intArrayOf(0)
        }
        val carryOutlines = (1 until preview.size step 3).map { i ->
            val track = preview[i]
            val start = preview[i + 1]
            val y = geometry.rowTop(track)
            Rect(geometry.x(start.toFloat()), y, geometry.x((start + preview[i + 2]).toFloat()),
                y + geometry.row - geometry.gap)
        }
        LaunchedEffect(carry?.released, timelineFrame) {
            if (carry?.released == true && timelineFrame != Rect.Zero) {
                if (selection != null && selectionStep != null) {
                    group = emptyList()
                    if (preview[0] == 1) menu = ArrangeMenu.Drop(selection, selectionStep)
                    else s.sendGridSelection(selection, selectionStep)
                } else if (target != null) s.arrangeAct(ArrangeAct.CARRY, carry.track, carry.clip, target.step,
                    if (target.insert) 1 else 0)
                s.arrangeCarry = null
            }
        }
        LaunchedEffect(carry?.pointer) {
            if (carry == null) return@LaunchedEffect
            val reach = with(density) { 40.dp.toPx() }
            val shift = with(density) { 14.dp.toPx() }
            while (true) {
                delay(33)
                val finger = s.arrangeCarry ?: break
                if (finger.released) break
                val local = finger.point - timelineFrame.topLeft
                if (local.y !in geo.rulerBottom..timelineFrame.height) continue
                val delta = when {
                    local.x < geo.header + reach && local.x >= 0 -> -shift
                    local.x > timelineFrame.width - reach && local.x <= timelineFrame.width -> shift
                    else -> 0f
                }
                if (delta != 0f) s.arrangeViewport = geo.port.copy(x = geo.clampX(geo.port.x + delta))
            }
        }
        Box(Modifier.fillMaxWidth().padding(top = toolbar + gap).height(visibleHeight)) {
            Canvas(Modifier.width(timelineWidth).fillMaxHeight().clipToBounds()
                .onGloballyPositioned { coordinates ->
                    s.body?.takeIf { it.isAttached }?.let { timelineFrame = it.localBoundingBoxOf(coordinates) }
                }.pointerInput(Unit) {
                var lastTap = -1_000L
                var lastPos = Offset.Zero
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val p = down.position
                    val g = geo
                    val initial = data
                    val referenceHit = g.refRow && p.y >= g.rulerBottom && p.y < g.bodyTop
                    val refFrom = ref?.offset ?: 0.0
                    val t = if (p.y < g.bodyTop) -1 else g.track(p.y)
                    val row = initial.tracks.getOrNull(t)
                    val step = g.step(p.x)
                    val headerHit = p.x < g.header
                    val cursorHit = t < 0 && !referenceHit && abs(p.x - g.x(s.arrangePosition.toFloat())) <= 14.dp.toPx()
                    val sectionEdge = if (!referenceHit && p.y >= g.ruler && p.y < g.rulerBottom && !headerHit) {
                        initial.sections.withIndex().firstNotNullOfOrNull { (i, section) ->
                            when {
                                abs(p.x - g.x(section.start.toFloat())) <= 8.dp.toPx() -> i to true
                                section.end >= 0 && abs(p.x - g.x(section.end.toFloat())) <= 8.dp.toPx() -> i to false
                                else -> null
                            }
                        }
                    } else null
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
                    var pinch: ArrangePinch? = null
                    down.consume()
                    while (true) {
                        val event = awaitPointerEvent()
                        val c = event.changes.firstOrNull { it.id == down.id } ?: break
                        if (c.isConsumed) break
                        val fingers = event.changes.filter { it.pressed }
                        val canPinch = !headerHit && !referenceHit && !lifting && (!moved || edge == 0)
                        if (fingers.size > 1 && (pinch != null || canPinch)) {
                            val a = fingers[0].position
                            val b = fingers[1].position
                            val current = pinch ?: ArrangePinch(a, b, geo, this.density, resizingTracks).also { pinch = it }
                            s.arrangeViewport = current.update(a, b)
                            marquee = null
                            outline = null
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
                                    referenceHit && !headerHit -> {
                                        refDrag?.let { AppNative.arrangeRefOffset(it) }
                                        s.refresh()
                                    }
                                    sectionEdge != null && !cursorHit -> s.arrangeAct(
                                        if (sectionEdge.second) ArrangeAct.SECTION_START else ArrangeAct.SECTION_END,
                                        sectionEdge.first, stepAtEnd)
                                    t < 0 && !headerHit && p.y < g.ruler && !cursorHit -> s.arrangeAct(ArrangeAct.LOOP_RANGE,
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
                                }
                            } else if (!moved) {
                                val double = c.uptimeMillis - lastTap < 320 && (p - lastPos).getDistance() < 30.dp.toPx()
                                lastTap = if (double) -1_000L else c.uptimeMillis
                                lastPos = p
                                if (referenceHit) {
                                    if (double && headerHit) menu = ArrangeMenu.Reference
                                } else if (t < 0 && !headerHit) {
                                    if (double) {
                                        val section = initial.sections.indexOfLast { it.start <= step }.takeIf { it >= 0 }
                                        menu = ArrangeMenu.Ruler(step.roundToInt(), section)
                                    } else s.arrangeAct(ArrangeAct.SEEK, step.roundToInt())
                                } else if (row != null) {
                                    when {
                                        headerHit -> Unit
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
                            referenceHit && !headerHit -> refDrag = refFrom + delta.x / g.stepWidth * stepDuration
                            cursorHit -> s.arrangeAct(ArrangeAct.SEEK, g.step(final.x).roundToInt())
                            selecting -> marquee = Rect(minOf(p.x, final.x), minOf(p.y, final.y), maxOf(p.x, final.x), maxOf(p.y, final.y))
                            b != null && (edge != 0 || lifting) -> {
                                val raw = g.step(final.x)
                                val at = if (initial.snap) (raw / initial.gridSteps).roundToInt() * initial.gridSteps else raw.roundToInt()
                                val unit = if (initial.snap) initial.gridSteps else 1
                                val deltaStep = ((raw - step) / unit).roundToInt() * unit
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
                                s.arrangeViewport = if (horizontalScroll) g.port.copy(x = g.clampX(g.port.x - delta.x))
                                    else g.port.copy(y = g.clampY(g.port.y - delta.y))
                            }
                        }
                    }
                    marquee = null
                    outline = null
                    refDrag = null
                }
            }) {
                drawArrangeTimeline(geometry, d, s, sectionNames, theme, measure, text, take,
                    group, marquee, carryOutlines + listOfNotNull(outline ?: target?.outline), reference, refDrag, bpm, stepSeconds)
                drawArrangeHeaders(geometry, d, s, names, reference, theme, measure, meters.value, text)
            }
            Box(Modifier.width(header).fillMaxHeight().arrangeHeaderTouches(s, geometry, meters, d.tracks.size) { menu = it })
            if (reference != null) {
                InkButton(if (d.refB) "B" else "A", d.refB, Modifier.align(Alignment.TopStart).width(44.dp)
                    .offset(x = header - 48.dp, y = 4.dp), 30f) { s.arrangeAct(ArrangeAct.REF_B) }
            }
        }
        if (lowerVisible) Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(gap)) {
            if (d.auto) ArrangeAutoPanel(s, d, geometry.port, header, lower, Modifier.fillMaxWidth()) {
                s.arrangeViewport = geometry.boundTime(it)
            } else steps()
        }
        if (knobs) knobsPanel(Modifier.width(timelineWidth).height(centerHeight).tile(theme.bg))
        ArrangeMeter(s, Modifier.align(Alignment.TopEnd).width(Layout.meterWidth.dp), centerHeight, lower, d.hideSteps)
        ArrangeToolbar(s, d, menu == ArrangeMenu.Grid, knobs,
            grid = { menu = if (menu == ArrangeMenu.Grid) null else ArrangeMenu.Grid },
            split = { if (group.size >= 2) groupAction(3) else s.arrangeAct(ArrangeAct.SPLIT) },
            toggleKnobs = { s.arrangeKnobs = !knobs })
        if (menu == ArrangeMenu.Grid) Box(Modifier.fillMaxSize().padding(top = toolbar + gap)) {
            ArrangeGridOverlay(s, d) { menu = null }
        }
    }
    menu?.takeUnless { it == ArrangeMenu.Grid }?.let { ArrangeMenus(s, d, it, sectionNames, { command -> groupAction(command) }) { menu = null } }
}
