package app.jambo.android.ui

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import kotlin.math.pow

internal data class SampleView(val left: Double = 0.0, val span: Double = 1.0) {
    val right get() = left + span
    fun fraction(x: Float, width: Int) = left + span * x / width.coerceAtLeast(1)
    fun x(fraction: Double, width: Float) = ((fraction - left) / span * width).toFloat()

    fun remap(begin: Double, end: Double): SampleView {
        val length = (end - begin).coerceAtLeast(1e-9)
        val nextSpan = (span / length).coerceAtMost(1.0)
        val nextLeft = ((left - begin) / length).coerceIn(0.0, 1 - nextSpan)
        return SampleView(nextLeft, nextSpan)
    }

    companion object {
        fun between(a: Double, b: Double, frames: Int): SampleView {
            val lo = minOf(a, b).coerceIn(0.0, 1.0)
            val hi = maxOf(a, b).coerceIn(0.0, 1.0)
            val span = (hi - lo).coerceIn(minSpan(frames), 1.0)
            return SampleView(lo.coerceAtMost(1 - span), span)
        }

        fun minSpan(frames: Int) = minOf(1.0, 256.0 / frames.coerceAtLeast(1))
    }
}

internal class SampleNavigation(
    private val initial: SampleView, private val down: Offset,
    private val width: Int, private val frames: Int, private val zoomPx: Float,
) {
    private var anchor: Offset? = null
    private var vertical = false

    fun move(p: Offset): SampleView? {
        val from = anchor
        if (from == null) {
            val delta = p - down
            vertical = abs(delta.y) > abs(delta.x)
            anchor = p
            return null
        }
        val delta = p - from
        val w = width.coerceAtLeast(1)
        val span = if (vertical) (initial.span * 2.0.pow((delta.y / zoomPx).toDouble()))
            .coerceIn(SampleView.minSpan(frames), 1.0) else initial.span
        val left = if (vertical) initial.fraction(from.x, width) - span * from.x / w
            else initial.left - initial.span * delta.x / w
        return SampleView(left.coerceIn(0.0, 1 - span), span)
    }
}

internal suspend fun AwaitPointerEventScope.trackSampleTouch(
    down: PointerInputChange, threshold: (Offset) -> Boolean, onMove: (Offset, Boolean) -> Unit,
): PointerInputChange? {
    down.consume()
    var moved = false
    while (true) {
        val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: return null
        if (change.isConsumed) return null
        if (!change.pressed) {
            change.consume()
            return change
        }
        if (change.position == change.previousPosition) continue
        val firstMove = !moved && threshold(change.position - down.position)
        if (!moved && !firstMove) continue
        moved = true
        change.consume()
        onMove(change.position, firstMove)
    }
}

internal class SampleDoubleTap {
    private var previous: PointerInputChange? = null
    private var previousFlag = -1

    fun accept(up: PointerInputChange, flag: Int, reach: Float): Boolean {
        val last = previous
        if (last != null && up.uptimeMillis - last.uptimeMillis < 350 &&
            abs(up.position.x - last.position.x) < reach && previousFlag == flag
        ) {
            previous = null
            return true
        }
        previous = up
        previousFlag = flag
        return false
    }
}

internal fun DrawScope.drawSampleScrollbar(view: SampleView, track: Color, accent: Color) {
    if (view.span >= 1) return
    val top = 1.dp.toPx()
    val height = 3.dp.toPx().coerceAtLeast(1f)
    val thumb = maxOf(6.dp.toPx(), (view.span * size.width).toFloat()).coerceAtMost(size.width)
    val left = (view.left * size.width).toFloat().coerceAtMost(size.width - thumb)
    drawRect(track, Offset(0f, top), Size(size.width, height))
    drawRect(accent, Offset(left, top), Size(thumb, height))
}
