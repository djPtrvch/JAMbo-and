package app.jambo.android.ui

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.PointerId
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import app.jambo.android.MidiTarget
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.roundToInt

private fun hit(g: GridGeom, d: GridData, x: Float, y: Float): GridHit {
    val r = (y / g.pitch).toInt()
    if (y < 0 || r >= d.windowRows || y - g.y(r) > g.rowH) return GridHit.None
    val k = d.rows[r]
    if (x < g.step) return if (k >= 0) GridHit.Scene(k) else if (d.plusRow == r) GridHit.Plus else GridHit.None
    if (x < g.x(0)) return GridHit.None
    val col = ((x - g.step - Layout.gap) / (g.trackW + Layout.gap)).toInt()
    if (col < 0 || col >= d.tracks || x < g.x(col) || x - g.x(col) > g.trackW || k < 0) return GridHit.None
    return GridHit.Cell(col, k)
}

/** Касание пальца: куда, где и когда начато, сдвинуто ли, удержано, прокрутка колонки строк. */
private class GridPress(val hit: GridHit, val start: Offset, val t0: Long, val top0: Int) {
    var moved = false
    var held = false
    var scrolling = false
    var job: Job? = null
}

private const val HOLD_MS = 350L
private const val DOUBLE_TAP_MS = 300L

internal suspend fun androidx.compose.ui.input.pointer.PointerInputScope.gridTouches(
    s: ScreenState, g: GridGeom, ui: GridUi, scope: CoroutineScope, dens: Float,
) {
    val presses = HashMap<PointerId, GridPress>()
    var dragId: PointerId? = null
    var bandId: PointerId? = null
    var bandPoint = Offset.Zero
    var bandMasks = IntArray(0)
    fun updateBand(press: GridPress, at: Offset, top: Int) {
        val shift = g.pitch * top
        val a = press.start.y + g.pitch * press.top0
        val b = at.y + shift
        val left = minOf(press.start.x, at.x).coerceAtLeast(0f)
        val right = maxOf(press.start.x, at.x).coerceAtMost(g.sideX)
        val rect = Rect(left, minOf(a, b), maxOf(left, right), maxOf(a, b))
        ui.selection = selectGrid(rect, g, bandMasks)
        ui.band = Rect(rect.left, (rect.top - shift).coerceIn(0f, g.h),
            rect.right, (rect.bottom - shift).coerceIn(0f, g.h))
    }
    fun beginBand(id: PointerId, press: GridPress, at: Offset) {
        bandId = id
        bandPoint = at
        bandMasks = AppNative.gridSelectionMasks()
        updateBand(press, at, GridData(s.grid).top)
        press.job?.cancel()
        press.job = scope.launch {
            while (bandId == id) {
                delay(250)
                if (bandId != id) break
                val d = GridData(s.grid)
                val delta = when {
                    bandPoint.y < g.rowH / 2 -> -1
                    bandPoint.y > g.h - g.rowH / 2 -> 1
                    else -> 0
                }
                val next = (d.top + delta).coerceIn(0, d.maxTop)
                if (next != d.top) {
                    s.act(Act.SET_GRID_TOP, next)
                    updateBand(press, bandPoint, next)
                }
            }
        }
    }
    awaitPointerEventScope {
        while (true) {
            val ev = awaitPointerEvent()
            for (c in ev.changes) {
                val x = c.position.x / dens
                val y = c.position.y / dens
                val d = GridData(s.grid)
                if (c.pressed && !c.previousPressed) {
                    val hit = hit(g, d, x, y)
                    if (s.midi.controls.learning) {
                        when (hit) {
                            is GridHit.Cell -> d.rows.indexOf(hit.row).takeIf { it >= 0 }
                                ?.let { s.learnTarget(MidiTarget.clip(hit.track, it)) }
                            is GridHit.Scene -> d.rows.indexOf(hit.row).takeIf { it >= 0 }
                                ?.let { s.learnTarget(MidiTarget.ROW + it) }
                            else -> Unit
                        }
                        c.consume()
                        continue
                    }
                    val p = GridPress(hit, Offset(x, y), System.currentTimeMillis(), d.top)
                    presses[c.id] = p
                    val id = c.id
                    p.job = scope.launch {
                        delay(HOLD_MS)
                        if (presses[id] !== p || p.moved) return@launch
                        p.held = true
                        when (val h = p.hit) {
                            is GridHit.Cell -> s.act(Act.HOLD_CELL, h.track, h.row)
                            is GridHit.Scene -> if (dragId == null) {
                                dragId = id
                                ui.dragRow = h.row
                                ui.rowTarget = h.row
                                ui.ghost = p.start
                            }
                            GridHit.None -> if (bandId == null && dragId == null) {
                                ui.selection = GridSelection()
                                beginBand(id, p, p.start)
                            }
                            else -> {}
                        }
                    }
                } else if (c.pressed) {
                    val p = presses[c.id] ?: continue
                    val dx = x - p.start.x
                    val dy = y - p.start.y
                    if (hypot(dx, dy) > 8) p.moved = true
                    val h = p.hit
                    if (bandId == c.id || h == GridHit.None && p.moved && bandId == null && dragId == null) {
                        val at = Offset(x, y)
                        if (bandId == null) beginBand(c.id, p, at)
                        else {
                            bandPoint = at
                            updateBand(p, at, d.top)
                        }
                        c.consume()
                        continue
                    }
                    if (h is GridHit.Scene || h is GridHit.Plus || h is GridHit.None) {
                        if (dragId == c.id) {
                            ui.ghost = Offset(g.step / 2, y)
                            ui.rowTarget = rowUnder(g, d, y)
                        } else if (!p.held && h is GridHit.Scene) {
                            if (!p.scrolling && p.moved && abs(dy) > abs(dx)) p.scrolling = true
                            if (p.scrolling) {
                                val top = (p.top0 + (-dy / g.pitch).roundToInt()).coerceIn(0, d.maxTop)
                                if (top != d.top) s.act(Act.SET_GRID_TOP, top)
                            }
                        }
                    } else if (h is GridHit.Cell && p.held && d.cells.isNotEmpty()) {
                        if (dragId != null && dragId != c.id) { c.consume(); continue }
                        if (dragId == null && hypot(dx, dy) <= 8) { c.consume(); continue }
                        dragId = c.id
                        val body = s.body
                        val field = s.patternTouchArea
                        val button = s.topRects["arrange"]
                        if (body?.isAttached == true && field?.isAttached == true && button != null) {
                            val rootPoint = body.localPositionOf(field, c.position)
                            if (button.contains(rootPoint)) {
                                val length = AppNative.arrangeCarryLength(h.track, h.row)
                                if (length > 0) {
                                    val selection = ui.selection.takeIf { it.group(h) }?.encode()
                                    s.arrangeCarry = ArrangeCarry(c.id, h.track, h.row, length, rootPoint, selection = selection)
                                    s.arrangeAct(ArrangeAct.TOGGLE)
                                    c.consume()
                                    continue
                                }
                            }
                        }
                        ui.dragCell = h
                        ui.ghost = Offset(x, y)
                        val t = hit(g, d, x, y)
                        ui.target = if (t is GridHit.Cell && t != h && !ui.selection.group(h)) t else null
                    }
                } else {
                    val p = presses.remove(c.id) ?: continue
                    p.job?.cancel()
                    if (bandId == c.id) {
                        bandId = null
                        ui.band = null
                        c.consume()
                        continue
                    }
                    var dropped: GridHit.Cell? = null
                    var droppedRow: Int? = null
                    if (dragId == c.id) {
                        dropped = ui.target
                        droppedRow = ui.rowTarget
                        dragId = null
                        ui.ghost = null
                        ui.target = null
                        ui.rowTarget = null
                        ui.dragCell = null
                        ui.dragRow = null
                    }
                    if (p.scrolling) { c.consume(); continue }
                    val h = p.hit
                    if (p.held) {
                        if (h is GridHit.Cell && dropped != null)
                            s.act(Act.DROP_CLIP, h.track, h.row, (dropped.track * 100 + dropped.row).toDouble())
                        if (h is GridHit.Scene && droppedRow != null && droppedRow != h.row)
                            s.act(Act.MOVE_ROW, h.row, droppedRow)
                        c.consume()
                        continue
                    }
                    val dx = x - p.start.x
                    val dy = y - p.start.y
                    // Свайп вверх по ячейке — меню ячейки; свайп вниз — ничего.
                    if (h is GridHit.Cell && -dy > 24 && abs(dx) < -dy) {
                        ui.cellMenu = h
                    } else if (hypot(dx, dy) <= 12) {
                        if (h is GridHit.Cell || h == GridHit.None) ui.selection = GridSelection()
                        val now = System.currentTimeMillis()
                        val last = ui.lastSceneTap
                        if (h is GridHit.Scene && last != null && last.first == h.row && now - last.second < DOUBLE_TAP_MS) {
                            // Двойной тап по строке — меню (первый тап уже запустил строку; ожидающий запуск — отмена).
                            ui.lastSceneTap = null
                            if (d.playing && d.sceneQueued == h.row) s.act(Act.QUEUE_SCENE, h.row)
                            ui.rowMenu = h.row
                        } else {
                            ui.lastSceneTap = if (h is GridHit.Scene) h.row to now else null
                            when (h) {
                                is GridHit.Cell -> s.act(Act.TAP_CELL, h.track, h.row)
                                is GridHit.Scene -> s.act(Act.QUEUE_SCENE, h.row)
                                GridHit.Plus -> s.act(Act.ADD_ROW)
                                GridHit.None -> {}
                            }
                        }
                    }
                }
                c.consume()
            }
        }
    }
}

/** Строка проекта под пальцем, куда встанет перетаскиваемая строка (только видимые строки). */
private fun rowUnder(g: GridGeom, d: GridData, y: Float): Int? {
    val r = (y / g.pitch).toInt().coerceIn(0, d.windowRows - 1)
    val k = d.rows.getOrElse(r) { -1 }
    return if (k >= 0 && k < d.visibleRows) k else null
}

