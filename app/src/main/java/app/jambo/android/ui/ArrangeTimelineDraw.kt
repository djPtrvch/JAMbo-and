package app.jambo.android.ui

import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import kotlin.math.floor

internal fun DrawScope.drawArrangeTimeline(
    geometry: TimelineGeometry, d: ArrangeData, s: ScreenState, sectionNames: Array<String>,
    theme: Theme, names: Array<String>, measure: TextMeasurer, text: TextUnit, take: IntArray,
    group: List<Pair<Int, Int>>, marquee: Rect?, outline: Rect?, reference: ArrangeReference?,
    refDrag: Double?, bpm: Double, stepSeconds: Double,
) {
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
            drawLine(theme.accent, Offset(x, g.ruler), Offset(x, g.rulerBottom))
            if (section.end >= 0) drawLine(theme.accent, Offset(x + w, g.ruler), Offset(x + w, g.rulerBottom))
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
                    style = Stroke(2.dp.toPx(), pathEffect = PathEffect.dashPathEffect(
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
        drawMarkerFlag(theme.accent, cursor - 8.dp.toPx(), 16.dp.toPx(), 0f, g.rulerBottom - 6.dp.toPx(),
            Offset(cursor, g.rulerBottom))
    }
    drawRect(theme.bg, size = Size(g.header, size.height))
    val seconds = (s.arrangePosition * 15 / bpm).toInt()
    val remaining = ((d.end - s.arrangePosition).coerceAtLeast(0.0) * 15 / bpm).toInt()
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
    reference?.let {
        drawArrangeReference(refDrag?.let { offset -> it.copy(offset = offset) } ?: it, g.header,
            g.rulerBottom, g.row - g.gap, g.stepWidth, g.port.x, stepSeconds, theme, measure, text)
    }
}
