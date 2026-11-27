package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlin.math.abs

internal val sliceRoleNames = mapOf(0 to "KICK", 1 to "SNARE", 2 to "HAT", 5 to "PERC", 6 to "BASS", 7 to "LEAD", 3 to "PAD")

@Composable
internal fun EditorWave(
    s: ScreenState, key: String, wave: FloatArray, marks: Int, labels: Array<String>, roles: IntArray, warp: WarpSample,
    view: SampleView, setView: (SampleView) -> Unit, start: Float, end: Float, heads: FloatArray, enabled: Boolean,
    select: (Float, Float) -> Unit, slice: (Int, Float) -> Unit,
) {
    val theme = LocalTheme.current
    val phone = LocalConfiguration.current.smallestScreenWidthDp < 600
    val flagWidth = if (phone) 56.dp else 36.dp
    val flagHeight = if (phone) 34.dp else 14.dp
    val flagStroke = if (phone) 2.dp else 1.dp
    val flagSlop = if (phone) 6.dp else 0.dp
    val trimGrip = if (phone) 32.dp else 22.dp
    val trimTarget = if (phone) 32.dp else 26.dp
    val trimStrip = if (phone) 56.dp else 44.dp
    val measure = rememberTextMeasurer()
    LaunchedEffect(s.refusedSlice) {
        if (s.refusedSlice >= 0) {
            delay(500)
            s.clearSliceRefusal()
        }
    }
    val currentView by rememberUpdatedState(view)
    val currentWarp by rememberUpdatedState(warp)
    val currentSetView by rememberUpdatedState(setView)
    val currentWave by rememberUpdatedState(wave)
    val currentMarks by rememberUpdatedState(marks)
    val selection by rememberUpdatedState(start to end)
    val setSelection by rememberUpdatedState(select)
    val setSlice by rememberUpdatedState(slice)
    var moving by remember(key) { mutableIntStateOf(-1) }
    var movingAt by remember(key) { mutableFloatStateOf(0f) }
    fun nearFlag(x: Float, y: Float, width: Int, half: Float, top: Float, bottom: Float, slop: Float): Int {
        if (y < top - slop || y > bottom + slop) return -1
        var closest = -1
        var distance = Float.POSITIVE_INFINITY
        for (i in currentMarks - 1 downTo 0) {
            val at = currentView.x(currentWave[3 + i].toDouble(), width.toFloat())
            if (at !in 0f..width.toFloat()) continue
            val left = (at - half).coerceIn(0f, maxOf(0f, width - 2 * half))
            if (x < left - slop || x > left + 2 * half + slop) continue
            if (!phone) return i
            val dx = abs(x - at)
            if (dx < distance) {
                closest = i
                distance = dx
            }
        }
        return closest
    }

    fun gripCenter(x: Float, width: Float, grip: Float, right: Boolean): Float {
        if (x !in 0f..width) return x
        val half = minOf(grip, width) / 2
        return (x + if (right) -half else half).coerceIn(half, width - half)
    }

    fun snap(at: Double, width: Int, pixels: Float): Double {
        val reach = currentView.span * pixels / width.coerceAtLeast(1)
        val closest = currentWarp.markers.minByOrNull { abs(it.at - at) }
        return if (closest != null && abs(closest.at - at) <= reach) closest.at else at
    }

    val input = if (!enabled) Modifier else Modifier.pointerInput(key, phone) {
        val taps = SampleDoubleTap()
        awaitEachGesture {
            val down = awaitFirstDown()
            val p = down.position
            val view0 = currentView
            val nav = SampleNavigation(view0, p, size.width, currentWarp.frames, 110.dp.toPx())
            val index = nearFlag(p.x, p.y, size.width, flagWidth.toPx() / 2, 4.dp.toPx(),
                (12.dp + flagHeight).toPx(), flagSlop.toPx())
            val width = size.width.toFloat()
            val ds = abs(p.x - gripCenter(view0.x(selection.first.toDouble(), width), width, trimGrip.toPx(), false))
            val de = abs(p.x - gripCenter(view0.x(selection.second.toDouble(), width), width, trimGrip.toPx(), true))
            val handle = if (index < 0 && p.y >= size.height - trimStrip.toPx() && minOf(ds, de) <= trimTarget.toPx()) {
                if (ds <= de) 0 else 1
            } else -1
            val x0 = view0.fraction(p.x, size.width)
            val markOffset = if (index >= 0) currentWave[3 + index] - x0 else 0.0
            val trimOffset = if (handle >= 0) (if (handle == 0) selection.first else selection.second) - x0 else 0.0
            var dragged = false
            if (index >= 0) s.holdSampleSlice(index)
            try {
                val up = trackSampleTouch(down, { it.getDistance() >= 10.dp.toPx() }) { position, firstMove ->
                    dragged = true
                    val x = view0.fraction(position.x, size.width).coerceIn(0.0, 1.0)
                    val gap = minOf(0.5f, 64f / currentWarp.frames.coerceAtLeast(1))
                    when {
                        index >= 0 -> {
                            if (firstMove) s.releaseSampleSlice()
                            movingAt = snap(x + markOffset, size.width, 12.dp.toPx()).coerceIn(0.0, 1.0).toFloat()
                            moving = index
                        }
                        handle == 0 -> setSelection((x + trimOffset).toFloat()
                            .coerceIn(0f, maxOf(0f, selection.second - gap)), selection.second)
                        handle == 1 -> setSelection(selection.first, (x + trimOffset).toFloat()
                            .coerceIn(minOf(1f, selection.first + gap), 1f))
                        else -> nav.move(position)?.let(currentSetView)
                    }
                }
                if (up != null) {
                    if (moving >= 0) setSlice(moving, movingAt)
                    else if (!dragged && handle < 0 && (index < 0 || !s.heldSliceUsed) &&
                        taps.accept(up, index, 24.dp.toPx())
                    ) {
                        val at = snap(view0.fraction(up.position.x, size.width), size.width, 12.dp.toPx())
                        setSlice(index, if (index >= 0) -1f else at.coerceIn(0.0, 0.999999).toFloat())
                    }
                }
            } finally {
                if (index >= 0 && s.heldSlice == index) s.releaseSampleSlice()
                moving = -1
            }
        }
    }
    Canvas(Modifier.fillMaxSize().then(input)) {
        val channels = wave[0].toInt()
        val lane = size.height / channels
        for (c in 0 until channels) {
            val y = (c + 0.5f) * lane
            drawLine(theme.dim, Offset(0f, y), Offset(size.width, y))
            for (i in 0 until editorColumns) {
                val x = size.width * (i + 0.5f) / editorColumns
                val k = 3 + marks + (c * editorColumns + i) * 2
                val low = wave[k].coerceIn(-1f, 1f) * lane * 0.45f
                val high = wave[k + 1].coerceIn(-1f, 1f) * lane * 0.45f
                drawLine(theme.text, Offset(x, y - high), Offset(x, y - low))
            }
        }
        fun x(at: Float) = view.x(at.toDouble(), size.width)
        val xStart = x(start).coerceIn(0f, size.width)
        val xEnd = x(end).coerceIn(0f, size.width)
        drawRect(theme.bg.copy(alpha = 0.64f), size = Size(xStart, size.height))
        drawRect(theme.bg.copy(alpha = 0.64f), Offset(xEnd, 0f), Size(size.width - xEnd, size.height))
        for ((i, f) in listOf(start, end).withIndex()) {
            val x = x(f)
            if (x !in 0f..size.width) continue
            drawLine(theme.accent, Offset(x, 0f), Offset(x, size.height), 2.dp.toPx())
            val grip = trimGrip.toPx().coerceAtMost(size.width)
            val center = gripCenter(x, size.width, grip, i == 1)
            val height = (if (phone) 30.dp else 22.dp).toPx()
            val top = size.height - height
            drawMarkerFlag(theme.accent, center - grip / 2, grip, size.height, top + 7.dp.toPx(),
                Offset(x.coerceIn(center - grip / 2 + grip * 0.2f, center + grip / 2 - grip * 0.2f), top))
            if (phone) {
                for (offset in listOf(-3.dp, 3.dp)) {
                    val xx = center + offset.toPx()
                    drawLine(theme.onAccent.copy(alpha = 0.8f), Offset(xx, top + height * 0.4f),
                        Offset(xx, top + height * 0.8f), 1.5.dp.toPx())
                }
            }
        }
        for (i in 0 until marks) {
            val x = x(if (i == moving) movingAt else wave[3 + i])
            if (x < 0 || x > size.width) continue
            val ink = if (s.refusedSlice == i) theme.rec else if (s.heldSlice == i || moving == i) theme.accent else theme.text
            val sizePx = flagWidth.toPx().coerceAtMost(size.width)
            val left = (x - sizePx / 2).coerceIn(0f, size.width - sizePx)
            val y = 4.dp.toPx()
            val shoulder = y + flagHeight.toPx()
            val tip = Offset(x.coerceIn(left + sizePx * 0.15f, left + sizePx * 0.85f), shoulder + 8.dp.toPx())
            drawLine(ink, Offset(x, shoulder), Offset(x, size.height), flagStroke.toPx())
            drawMarkerFlag(ink, left, sizePx, y, shoulder, tip)
            val role = roles.getOrNull(i)?.let { sliceRoleNames[it] }
            val label = labels.getOrElse(i) { "" }
            val layout = measure.measure(if (role != null && role == label) "NO NOTE" else label, TextStyle(color = theme.bg,
                fontSize = if (phone) 12.sp else 9.sp))
            drawText(layout, topLeft = Offset(left + (sizePx - layout.size.width) / 2,
                y + (flagHeight.toPx() - layout.size.height) / 2))
            if (role != null) {
                val active = s.heldSlice == i || moving == i
                val tag = measure.measure(role, TextStyle(color = if (active) theme.onAccent else theme.text,
                    fontSize = if (phone) 13.sp else 10.sp, fontWeight = FontWeight.SemiBold))
                val pad = 3.dp.toPx()
                val badgeWidth = tag.size.width + 2 * pad
                val tagX = (x + 4.dp.toPx()).coerceIn(0f, maxOf(0f, size.width - badgeWidth))
                val tagY = tip.y + 4.dp.toPx()
                drawRect(if (active) theme.accent else theme.bg, Offset(tagX, tagY),
                    Size(badgeWidth, tag.size.height + 2 * pad))
                drawText(tag, topLeft = Offset(tagX + pad, tagY + pad))
            }
        }
        for (mark in warp.markers) {
            val x = x(mark.at.toFloat())
            drawLine(theme.dim, Offset(x, size.height - 24.dp.toPx()), Offset(x, size.height), 1.dp.toPx())
        }
        for (i in 0 until heads.size - 1 step 2) {
            val x = x(heads[i])
            drawLine(theme.text.copy(alpha = heads[i + 1].coerceIn(0f, 1f)),
                Offset(x, 0f), Offset(x, size.height), 1.dp.toPx())
        }
        drawSampleScrollbar(view, theme.panel2, theme.accent)
    }
}
