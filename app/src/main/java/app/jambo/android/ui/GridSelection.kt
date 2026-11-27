package app.jambo.android.ui

import androidx.compose.ui.geometry.Rect
import app.jambo.android.AppNative

internal data class GridSelection(
    val rows: Set<Int> = emptySet(), val clips: Set<GridHit.Cell> = emptySet(),
) {
    fun contains(cell: GridHit.Cell) = cell.row in rows || cell in clips
    fun touches(row: Int) = row in rows || clips.any { it.row == row }
    fun group(cell: GridHit.Cell) = contains(cell) && (rows.isNotEmpty() || clips.size > 1)
    fun encode() = (rows.flatMap { listOf(it, -1) } + clips.flatMap { listOf(it.row, it.track) }).toIntArray()
}

internal fun selectGrid(rect: Rect, g: GridGeom, masks: IntArray): GridSelection {
    val rows = mutableSetOf<Int>()
    val clips = mutableSetOf<GridHit.Cell>()
    for ((row, mask) in masks.withIndex()) {
        val y = g.y(row)
        if (rect.bottom < y || rect.top > y + g.rowH) continue
        if (rect.left <= g.step && rect.right >= 0f) rows.add(row)
        for (track in 0 until g.tracks) {
            val x = g.x(track)
            if ((mask and (1 shl track)) != 0 && rect.left <= x + g.trackW && rect.right >= x) {
                clips.add(GridHit.Cell(track, row))
            }
        }
    }
    return GridSelection(rows, clips)
}

internal fun ScreenState.sendGridSelection(group: IntArray, step: Int = -1, mode: Int = 0) {
    AppNative.arrangeSendSelection(group, step, mode)
    refresh()
}
