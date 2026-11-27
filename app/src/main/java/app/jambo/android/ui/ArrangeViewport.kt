package app.jambo.android.ui

import androidx.compose.ui.geometry.Offset
import kotlin.math.abs
import kotlin.math.floor

// Нулевая ширина шага и высота строки означают автоматический размер.
internal data class ArrangeViewport(val x: Float = 0f, val y: Float = 0f, val stepWidth: Float = 0f, val rowHeight: Float = 0f)

// Метрики ArrMetrics из Swift. Высота окна до перекрытия задаёт EXPANDED и предел SCALE;
// высота видимой части определяет COMPACT и предел вертикальной прокрутки.
internal class TimelineGeometry(
    val header: Float, val ruler: Float, val sectionBand: Float, val gap: Float,
    val width: Float, fullHeight: Float, visibleHeight: Float, tracks: Int, timelineEnd: Int,
    val refRow: Boolean, expanded: Boolean, private val density: Float, viewport: ArrangeViewport,
) {
    val rulerBottom = ruler + sectionBand
    private val fullArea = (fullHeight - rulerBottom).coerceAtLeast(0f)
    private val seenArea = (visibleHeight - rulerBottom).coerceAtLeast(0f)
    private val lanes = tracks.coerceAtLeast(1) + if (refRow) 1 else 0
    private val timelineSteps = timelineEnd.coerceAtLeast(0) + 32f * 16
    val stepWidth = clampStep(viewport.stepWidth.takeIf { it > 0 } ?: width / 256f)
    private val rowHeight = when {
        expanded && viewport.rowHeight > 0 -> clampRow(viewport.rowHeight)
        expanded -> ((fullArea - 7 * gap) / 8).coerceAtLeast(20 * density)
        else -> ((seenArea - (lanes - 1) * gap) / lanes).coerceAtLeast(16 * density)
    }
    val row = rowHeight + gap
    val bodyTop = trackTop(row)
    val port = viewport.copy(
        x = clampX(viewport.x), y = clampY(viewport.y),
        stepWidth = if (viewport.stepWidth > 0) stepWidth else 0f,
        rowHeight = if (expanded && viewport.rowHeight > 0) rowHeight else 0f,
    )

    fun clampStep(value: Float) = value.coerceIn(width / 1024f, width / 64f)
    fun clampRow(value: Float) = value.coerceIn(16 * density, fullArea.coerceAtLeast(16 * density))
    fun clampX(value: Float, step: Float = stepWidth) = value.coerceIn(0f, (timelineSteps * step - width).coerceAtLeast(0f))
    fun clampY(value: Float, pitch: Float = row) = value.coerceIn(0f, (lanes * pitch - gap - seenArea).coerceAtLeast(0f))
    fun boundTime(viewport: ArrangeViewport): ArrangeViewport {
        val step = clampStep(viewport.stepWidth.takeIf { it > 0 } ?: width / 256f)
        return viewport.copy(x = clampX(viewport.x, step), stepWidth = if (viewport.stepWidth > 0) step else 0f)
    }

    fun trackTop(pitch: Float) = rulerBottom + if (refRow) pitch else 0f
    fun x(step: Float) = header + step * stepWidth - port.x
    fun step(x: Float) = (x - header + port.x) / stepWidth
    fun track(y: Float) = floor((y - bodyTop + port.y) / row).toInt()
    fun rowTop(track: Int) = bodyTop + track * row - port.y
}

internal class ArrangePinch(
    first: Offset, second: Offset, private val geometry: TimelineGeometry,
    private val density: Float, private val scaleTracks: Boolean,
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
            if (maxOf(dx, dy) < 10 * density) {
                return g.port.copy(x = g.clampX(g.port.x + baseMid.x - mid.x))
            }
            vertical = scaleTracks && dy > dx
        }
        if (vertical == true) {
            val next = g.clampRow((g.row - g.gap) * y / spreadY.coerceAtLeast(20 * density))
            val pitch = next + g.gap
            val anchor = (g.port.y + baseMid.y - g.bodyTop) / g.row
            return g.port.copy(rowHeight = next, y = g.clampY(anchor * pitch + g.trackTop(pitch) - mid.y, pitch))
        }
        val next = g.clampStep(g.stepWidth * x / spreadX.coerceAtLeast(20 * density))
        val anchor = g.step(baseMid.x)
        return g.port.copy(stepWidth = next, x = g.clampX(anchor * next + g.header - mid.x, next))
    }
}
