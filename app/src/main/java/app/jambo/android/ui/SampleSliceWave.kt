package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlin.math.abs

@Composable
internal fun EditorWave(
    s: ScreenState, key: String, wave: FloatArray, marks: Int, labels: Array<String>, warp: WarpSample,
    view: SampleView, setView: (SampleView) -> Unit, start: Float, end: Float, heads: FloatArray, enabled: Boolean,
    select: (Float, Float) -> Unit, slice: (Int, Float) -> Unit,
) {
    val theme = LocalTheme.current
    val phone = LocalConfiguration.current.smallestScreenWidthDp < 600
    val flagWidth = if (phone) 48.dp else 36.dp
    val flagHeight = if (phone) 30.dp else 14.dp
    val flagStroke = if (phone) 2.dp else 1.dp
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
    fun nearFlag(x: Float, y: Float, width: Int, half: Float, top: Float, bottom: Float): Int {
        if (y < top || y > bottom) return -1
        return (currentMarks - 1 downTo 0).firstOrNull {
            val at = currentView.x(currentWave[3 + it].toDouble(), width.toFloat())
            val left = (at - half).coerceIn(0f, maxOf(0f, width - 2 * half))
            at in 0f..width.toFloat() && x >= left && x <= left + 2 * half
        } ?: -1
    }

    fun snap(at: Double, width: Int, pixels: Float): Double {
        val reach = currentView.span * pixels / width.coerceAtLeast(1)
        val closest = currentWarp.markers.minByOrNull { abs(it.at - at) }
        return if (closest != null && abs(closest.at - at) <= reach) closest.at else at
    }

    val input = if (!enabled) Modifier else Modifier
        .pointerInput(key, phone) {
            detectTapGestures(
                onPress = { p ->
                    val index = nearFlag(p.x, p.y, size.width, flagWidth.toPx() / 2, 4.dp.toPx(), (4.dp + flagHeight).toPx())
                    if (index >= 0 || s.heldSlice < 0) s.holdSampleSlice(index)
                    try { tryAwaitRelease() } finally { if (s.heldSlice == index) s.releaseSampleSlice() }
                },
                onDoubleTap = { p ->
                    if (!s.heldSliceUsed) {
                        val index = nearFlag(p.x, p.y, size.width, flagWidth.toPx() / 2, 4.dp.toPx(), (4.dp + flagHeight).toPx())
                        val at = snap(currentView.fraction(p.x, size.width), size.width, 12.dp.toPx())
                        setSlice(index, if (index >= 0) -1f else at.coerceIn(0.0, 0.999999).toFloat())
                    }
                },
            )
        }
        .pointerInput(key, phone) {
            var handle = -1
            var markOffset = 0.0
            var view0 = SampleView()
            var anchor = Offset.Zero
            var vertical: Boolean? = null
            detectDragGestures(
                onDragStart = { p ->
                    view0 = currentView
                    anchor = p
                    vertical = null
                    val x = view0.fraction(p.x, size.width)
                    moving = nearFlag(p.x, p.y, size.width, flagWidth.toPx() / 2, 4.dp.toPx(), (4.dp + flagHeight).toPx())
                    if (moving >= 0) {
                        movingAt = currentWave[3 + moving]
                        markOffset = movingAt - x
                        s.releaseSampleSlice()
                    }
                    val reach = view0.span * 26.dp.toPx() / size.width.coerceAtLeast(1)
                    val ds = abs(x - selection.first)
                    val de = abs(x - selection.second)
                    handle = if (moving < 0 && p.y >= size.height - 44.dp.toPx() && minOf(ds, de) <= reach) {
                        if (ds <= de) 0 else 1
                    } else -1
                },
                onDragEnd = {
                    if (moving >= 0) setSlice(moving, movingAt)
                    moving = -1
                },
                onDragCancel = { moving = -1 },
            ) { change, _ ->
                change.consume()
                val p = change.position
                val x = view0.fraction(p.x, size.width).coerceIn(0.0, 1.0)
                val gap = minOf(0.5f, 64f / currentWarp.frames.coerceAtLeast(1))
                if (moving >= 0) movingAt = snap(x + markOffset, size.width, 12.dp.toPx()).coerceIn(0.0, 1.0).toFloat()
                else if (handle == 0) setSelection(x.toFloat().coerceIn(0f, maxOf(0f, selection.second - gap)), selection.second)
                else if (handle == 1) setSelection(selection.first, x.toFloat().coerceIn(minOf(1f, selection.first + gap), 1f))
                else {
                    if (vertical == null) vertical = abs(p.y - anchor.y) > abs(p.x - anchor.x)
                    currentSetView(editView(view0, p.x - anchor.x, p.y - anchor.y, anchor.x,
                        size.width, vertical == true, currentWarp.frames, 110.dp.toPx()))
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
        for (f in listOf(start, end)) {
            val x = x(f)
            drawLine(theme.accent, Offset(x, 0f), Offset(x, size.height), 2.dp.toPx())
            drawCircle(theme.accent, 6.dp.toPx(), Offset(x, size.height - 14.dp.toPx()))
        }
        for (i in 0 until marks) {
            val x = x(if (i == moving) movingAt else wave[3 + i])
            if (x < 0 || x > size.width) continue
            val ink = if (s.refusedSlice == i) theme.rec else if (s.heldSlice == i || moving == i) theme.accent else theme.text
            val sizePx = flagWidth.toPx().coerceAtMost(size.width)
            val left = (x - sizePx / 2).coerceIn(0f, size.width - sizePx)
            val y = 4.dp.toPx()
            drawLine(ink, Offset(x, y), Offset(x, size.height), flagStroke.toPx())
            drawRect(ink, Offset(left, y), Size(sizePx, flagHeight.toPx()))
            val layout = measure.measure(labels.getOrElse(i) { "" }, TextStyle(color = theme.bg,
                fontSize = if (phone) 12.sp else 9.sp))
            drawText(layout, topLeft = Offset(left + (sizePx - layout.size.width) / 2,
                y + (flagHeight.toPx() - layout.size.height) / 2))
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
    }
}
