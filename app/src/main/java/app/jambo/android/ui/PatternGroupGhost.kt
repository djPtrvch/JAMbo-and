package app.jambo.android.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.unit.dp

internal data class PatternGhostTile(val track: Int, val row: Int, val cell: GridCell)

internal fun patternGhostTiles(data: GridData, selection: GridSelection): List<PatternGhostTile> = buildList {
    for ((rowIndex, row) in data.rows.withIndex()) {
        if (row < 0 || !selection.touches(row)) continue
        for ((track, cell) in data.cells[rowIndex].withIndex()) {
            if (cell.filled && selection.contains(GridHit.Cell(track, row))) add(PatternGhostTile(track, rowIndex, cell))
        }
    }
}

// Одна запись отрисовки группы и одна тень. Положение пальца читается только в фазе рисования.
@Composable
internal fun PatternGroupGhost(s: ScreenState, d: GridData, g: GridGeom, ui: GridUi, modifier: Modifier) {
    val selection = ui.selection
    val tiles = remember(d, selection) { patternGhostTiles(d, selection) }
    val origin = ui.groupOrigin ?: return
    val th = LocalTheme.current
    Box(modifier.drawWithCache {
        val radius = CornerRadius(6.dp.toPx())
        val width = g.trackW.dp.toPx()
        val height = g.rowH.dp.toPx()
        val bounds = tiles.map { tile ->
            val center = Offset(g.x(tile.track).dp.toPx() + width / 2, g.y(tile.row).dp.toPx() + height / 2)
            val half = Offset(width * 0.53f, height * 0.53f)
            Rect(center - half, center + half)
        }
        val shadowPath = Path().apply {
            bounds.forEach { addRoundRect(RoundRect(it, radius)) }
        }
        val shadow = Paint().apply {
            color = th.panel2
            asFrameworkPaint().setShadowLayer(10.dp.toPx(), 0f, 2.dp.toPx(), 0x80000000.toInt())
        }
        val stroke = Stroke(1.dp.toPx())
        val progressHeight = 3.dp.toPx()
        onDrawBehind {
            val finger = ui.ghost ?: return@onDrawBehind
            val delta = (finger - origin) * density
            translate(delta.x, delta.y) {
                drawIntoCanvas { it.drawPath(shadowPath, shadow) }
                tiles.forEachIndexed { i, tile ->
                    val rect = bounds[i]
                    val alpha = if (!d.playing || !tile.cell.queued || s.blink) 1f else 0.35f
                    drawRoundRect(clipTileBackground(tile.cell, th).copy(alpha = alpha), rect.topLeft, rect.size, radius)
                    drawRoundRect(th.text.copy(alpha = alpha), rect.topLeft, rect.size, radius, style = stroke)
                    drawClipThumbnail(tile.cell, th, rect.topLeft + Offset(6.dp.toPx(), 5.dp.toPx()),
                        Size((rect.width - 12.dp.toPx()).coerceAtLeast(0f), (rect.height - 12.dp.toPx()).coerceAtLeast(0f)),
                        alpha)
                    if (d.playing && tile.cell.active) {
                        val progress = (s.gridLive.getOrElse(2 + tile.track) { 0 } / 1000f).coerceIn(0f, 1f)
                        drawRect(th.accent.copy(alpha = alpha), Offset(rect.left, rect.bottom - progressHeight),
                            Size(rect.width * progress, progressHeight))
                    }
                }
            }
        }
    })
}

internal fun clipTileBackground(cell: GridCell, theme: Theme): Color =
    if (!cell.filled) theme.panel else if (cell.active) theme.panel2 else theme.panel2.copy(alpha = 0.62f)

/** Миниатюра клипа: страницы строками по 16 шагов, по высоте — не меньше 4 строк, по центру. */
internal fun DrawScope.drawClipThumbnail(cell: GridCell, theme: Theme, origin: Offset, area: Size, alpha: Float = 1f) {
    val rows = cell.pageCount
    val units = maxOf(rows, 4)
    val cw = area.width / 16
    val rh = area.height / units
    val top = (units - rows) / 2f * rh
    val radius = CornerRadius(1.dp.toPx())
    val mark = Size(cw * 0.76f, rh * 0.76f)
    for (row in 0 until rows) {
        val bits = cell.pages[2 * row]
        val outside = cell.pages[2 * row + 1]
        for (step in 0 until 16) {
            val ink = when {
                bits shr step and 1 != 0 -> theme.text
                bits shr (step + 16) and 1 != 0 -> theme.text.copy(alpha = 0.45f)
                else -> theme.off
            }
            val opacity = if (outside shr step and 1 != 0) 0.2f else 1f
            drawRoundRect(ink.copy(alpha = ink.alpha * alpha * opacity),
                origin + Offset(step * cw + cw * 0.12f, top + row * rh + rh * 0.12f), mark, radius)
        }
    }
}
