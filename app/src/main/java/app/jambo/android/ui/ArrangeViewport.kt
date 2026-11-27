package app.jambo.android.ui

import androidx.compose.ui.geometry.Offset
import kotlin.math.abs

internal data class ArrangeViewport(val x: Float = 0f, val y: Float = 0f, val stepWidth: Float = 0f, val rowHeight: Float = 0f)

internal class ArrangePinch(
    first: Offset, second: Offset, private val geometry: TimelineGeometry,
    private val width: Float, private val height: Float, private val density: Float,
    private val scaleTracks: Boolean, private val trackCount: Int,
) {
    private val baseMid = (first + second) / 2f
    private val spreadX = abs(first.x - second.x)
    private val spreadY = abs(first.y - second.y)
    private var vertical: Boolean? = null

    fun update(first: Offset, second: Offset): ArrangeViewport {
        val g = geometry
        val mid = (first + second) / 2f
        val x = abs(first.x - second.x)
        val y = abs(first.y - second.y)
        if (vertical == null) {
            val dx = abs(x - spreadX)
            val dy = abs(y - spreadY)
            if (maxOf(dx, dy) < 8f * density) {
                return g.port.copy(x = (g.port.x + baseMid.x - mid.x).coerceAtLeast(0f))
            }
            vertical = scaleTracks && dy > dx
        }
        if (vertical == true) {
            val minRow = 16 * density
            val maxRow = (height - g.rulerBottom).coerceAtLeast(minRow)
            val next = ((g.row - g.gap) * y / spreadY.coerceAtLeast(20 * density)).coerceIn(minRow, maxRow)
            val pitch = next + g.gap
            val top = g.rulerBottom + if (g.refRow) pitch else 0f
            val fixedRow = (g.port.y + baseMid.y - g.bodyTop) / g.row
            val maxScroll = (trackCount * pitch - g.gap + top - height).coerceAtLeast(0f)
            return g.port.copy(rowHeight = next, y = (fixedRow * pitch + top - mid.y).coerceIn(0f, maxScroll))
        }
        val next = (g.stepWidth * x / spreadX.coerceAtLeast(20 * density)).coerceIn(width / 1024f, width / 64f)
        val anchor = g.step(baseMid.x)
        return g.port.copy(stepWidth = next, x = (anchor * next + g.header - mid.x).coerceAtLeast(0f))
    }
}
