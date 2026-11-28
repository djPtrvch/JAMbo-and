package app.jambo.android.ui

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import app.jambo.android.MidiTarget
import kotlin.math.abs
import kotlin.math.roundToInt

// Геометрия регулятора, имени и кнопок общая для рисунка и касаний.
private class HeaderRow(g: TimelineGeometry, top: Float, density: Float) {
    private val h = g.row - g.gap
    private val controlsHeight = (22 + 4 + 15) * density
    private val volume = h >= controlsHeight + 8 * density
    val pad = ((h - if (volume) controlsHeight else 15 * density) / 2).coerceIn(2 * density, 8 * density)
    val bounds = Rect(0f, top, g.header, top + h)
    private val button = (h - 2 * pad - if (volume) 26 * density else 0f).coerceIn(10 * density, 24 * density)
    private val buttonBottom = bounds.bottom - pad
    val solo = Rect(bounds.right - pad - button - 2 * density, buttonBottom - button, bounds.right - pad, buttonBottom)
    val mute = solo.translate(-(solo.width + 4 * density), 0f)
    val fader = if (volume) Rect(pad, top + pad, (bounds.right - pad - 34 * density).coerceAtLeast(pad + 1),
        top + pad + 22 * density) else null
}

internal class ArrangeHeaderMeters(private val values: FloatArray) {
    fun at(track: Int, field: Int) = values.getOrElse(track * 7 + field) { 0f }
    fun volume(track: Int) = at(track, 0)
}

@Composable
internal fun Modifier.arrangeHeaderTouches(
    s: ScreenState, geometry: TimelineGeometry, meters: State<ArrangeHeaderMeters>, tracks: Int,
    open: (ArrangeMenu) -> Unit,
): Modifier {
    val current by rememberUpdatedState(geometry)
    val count by rememberUpdatedState(tracks)
    val menu by rememberUpdatedState(open)
    return pointerInput(s) {
        var lastTrack = Int.MIN_VALUE
        var lastTap = -1_000L
        awaitEachGesture {
            val down = awaitFirstDown()
            val g = current
            val p = down.position
            if (p.y < g.rulerBottom) return@awaitEachGesture
            val index = if (g.refRow && p.y < g.bodyTop) -1 else g.track(p.y)
            val header = when (index) {
                -1 -> if (g.refRow && p.y < g.bodyTop) HeaderRow(g, g.rulerBottom, density) else null
                in 0 until count -> HeaderRow(g, g.rowTop(index), density)
                else -> null
            }
            val body = header?.takeIf { it.bounds.contains(p) }
            val meter = body?.fader?.takeIf { it.contains(p) }
            val initial = meters.value.volume(if (index == -1) count else index)
            var scrolling = false
            var changingVolume = false
            down.consume()
            try {
                while (true) {
                    val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                    if (change.isConsumed) break
                    val delta = change.position - p
                    if (!change.pressed) {
                        if (!scrolling && !changingVolume && delta.getDistance() < 8.dp.toPx() && body != null) {
                            when {
                                index >= 0 && body.mute.contains(p) -> {
                                    if (!s.learnTarget(MidiTarget.MUTE + index)) AppNative.mixAct(1, index, 0, "")
                                    s.refresh()
                                    lastTrack = Int.MIN_VALUE
                                }
                                index >= 0 && body.solo.contains(p) -> {
                                    if (!s.learnTarget(MidiTarget.SOLO + index)) AppNative.mixAct(2, index, 0, "")
                                    s.refresh()
                                    lastTrack = Int.MIN_VALUE
                                }
                                index == lastTrack && change.uptimeMillis - lastTap < 300 -> {
                                    menu(if (index == -1) ArrangeMenu.Reference else ArrangeMenu.Track(index))
                                    lastTrack = Int.MIN_VALUE
                                }
                                else -> {
                                    if (index >= 0) s.arrangeAct(ArrangeAct.SELECT, index, -1)
                                    lastTrack = index
                                    lastTap = change.uptimeMillis
                                }
                            }
                        }
                        change.consume()
                        break
                    }
                    if (!scrolling && !changingVolume && delta.getDistance() >= 8.dp.toPx()) {
                        if (meter != null && abs(delta.x) > abs(delta.y)) {
                            changingVolume = true
                            s.act(Act.BEGIN_GESTURE)
                        } else if (abs(delta.y) >= 8.dp.toPx()) scrolling = true
                    }
                    if (scrolling) {
                        s.arrangeViewport = g.port.copy(y = g.clampY(g.port.y - delta.y))
                        change.consume()
                    } else if (changingVolume && meter != null) {
                        AppNative.setArrangeHeaderVolume(index, (initial + delta.x / meter.width * 60).coerceIn(-60f, 0f).toDouble())
                        s.refresh()
                        change.consume()
                    }
                }
            } finally {
                if (changingVolume) s.act(Act.END_GESTURE)
                if (scrolling || changingVolume) lastTrack = Int.MIN_VALUE
            }
        }
    }
}

internal fun DrawScope.drawArrangeHeaders(
    g: TimelineGeometry, d: ArrangeData, s: ScreenState, names: Array<String>, reference: ArrangeReference?,
    theme: Theme, measure: TextMeasurer, meters: ArrangeHeaderMeters, text: TextUnit,
) {
    clipRect(right = g.header, top = g.bodyTop) {
        d.tracks.forEachIndexed { index, lane ->
            val row = HeaderRow(g, g.rowTop(index), density)
            if (row.bounds.bottom <= g.bodyTop || row.bounds.top >= size.height) return@forEachIndexed
            val selected = s.tracks[2] == 0 && s.tracks[1] == index
            drawHeader(row, names.getOrElse(index) { (index + 1).toString() }, lane, selected,
                index, theme, measure, meters, text)
            if (selected) {
                for (y in listOf(row.bounds.top, row.bounds.bottom - 1.dp.toPx()))
                    drawLine(theme.accent, Offset(0f, y), Offset(g.header, y), 1.dp.toPx())
            }
        }
    }
    reference?.let {
        val tempo = if (it.bpm > 0) " · %.1f BPM".format(it.bpm) else ""
        drawHeader(HeaderRow(g, g.rulerBottom, density), "REF · " + it.name + tempo, null, false,
            d.tracks.size, theme, measure, meters, text)
    }
}

private fun DrawScope.drawHeader(
    row: HeaderRow, name: String, lane: ArrangeTrack?, selected: Boolean, index: Int,
    theme: Theme, measure: TextMeasurer, meters: ArrangeHeaderMeters, text: TextUnit,
) {
    drawRect(theme.trackBg, row.bounds.topLeft, row.bounds.size)
    val nameWidth = if (lane == null) row.bounds.width - 2 * row.pad else row.mute.left - 2 * row.pad
    val label = measure.measure(name, TextStyle(color = theme.text, fontSize = (text.value * 12 / 13).sp,
        fontWeight = if (selected) FontWeight.ExtraBold else FontWeight.SemiBold),
        maxLines = 1, overflow = TextOverflow.Ellipsis, constraints = Constraints(maxWidth = nameWidth.toInt().coerceAtLeast(1)))
    drawText(label, topLeft = Offset(row.pad, row.bounds.bottom - row.pad - label.size.height))
    if (lane != null) {
        drawHeaderToggle("M", lane.mute, theme.rec, row.mute, Color.White, measure, text)
        drawHeaderToggle("S", lane.solo, theme.accent, row.solo, theme.onAccent, measure, text)
    }
    row.fader?.let { rect ->
        val db = meters.volume(index).coerceIn(-60f, 0f)
        val thumb = rect.left + (db + 60) / 60 * rect.width
        val stereo = lane?.stereo != false
        val span = 4.dp.toPx()
        val bars = if (stereo) 2 else 1
        val color = if (meters.at(index, 6) > 0) theme.text else theme.dim
        repeat(bars) { ch ->
            val y = rect.center.y - (bars * span + (bars - 1) * 2.dp.toPx()) / 2 + ch * (span + 2.dp.toPx())
            val pos = Offset(rect.left, y)
            val level = if (stereo) meters.at(index, 1 + ch * 2) else maxOf(meters.at(index, 1), meters.at(index, 3))
            val hold = if (stereo) meters.at(index, 2 + ch * 2) else maxOf(meters.at(index, 2), meters.at(index, 4))
            drawRect(theme.meterTrack, pos, Size(rect.width, span))
            drawRect(color, pos, Size(rect.width * level.coerceIn(0f, 1f), span))
            val peak = rect.left + rect.width * hold.coerceIn(0f, 1f)
            if (hold > 0) drawLine(color, Offset(peak, y), Offset(peak, y + span), 1.dp.toPx())
        }
        val duck = (-meters.at(index, 5) / 24).coerceIn(0f, 1f) * rect.width
        if (duck > 0) drawRect(Color(0xffffa040), Offset(rect.right - duck, rect.bottom - 2.dp.toPx()), Size(duck, 2.dp.toPx()))
        drawRoundRect(theme.text, Offset(thumb - 4.dp.toPx(), rect.top), Size(8.dp.toPx(), rect.height), CornerRadius(3.dp.toPx()))
        val value = if (db <= -59.95f) "−∞" else "${db.roundToInt()} dB"
        val readout = measure.measure(value, TextStyle(color = theme.dim, fontSize = (text.value * 9 / 13).sp))
        drawText(readout, topLeft = Offset(row.bounds.right - row.pad - readout.size.width,
            rect.center.y - readout.size.height / 2f))
    }
}

private fun DrawScope.drawHeaderToggle(
    title: String, on: Boolean, color: Color, rect: Rect, ink: Color, measure: TextMeasurer, label: TextUnit,
) {
    val radius = CornerRadius(4.dp.toPx())
    if (on) drawRoundRect(color, rect.topLeft, rect.size, radius)
    drawRoundRect(color, rect.topLeft, rect.size, radius, style = Stroke(1.dp.toPx()))
    val text = measure.measure(title, TextStyle(color = if (on) ink else color,
        fontSize = ((rect.height / density / 2).coerceAtLeast(7f) * label.value / 13).sp, fontWeight = FontWeight.Bold))
    drawText(text, topLeft = rect.center - Offset(text.size.width / 2f, text.size.height / 2f))
}
