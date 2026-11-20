package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.roundToInt

/** Геометрия сетки (GridGeometry в PatternGrid.swift): колонка строк над MIX, столбцы — ровно над дорожками, справа
 *  (над MST) — кнопки; 8 строк с зазором 4. Всё — в dp. */
class GridGeom(val w: Float, val h: Float, val step: Float, val tracks: Int, val rows: Int) {
    val rowH = (h - (rows - 1) * ROW_GAP) / maxOf(1, rows)
    val pitch = rowH + ROW_GAP
    val trackW = (w - 2 * step - 2 * Layout.gap - maxOf(0, tracks - 1) * Layout.gap) / maxOf(1, tracks)
    fun x(col: Int) = if (col < 0) 0f else step + Layout.gap + col * (trackW + Layout.gap)
    fun y(row: Int) = row * pitch
    val sideX get() = w - step

    companion object {
        const val ROW_GAP = 4f
    }
}

/** Куда попало касание: ячейка (дорожка, строка проекта), строка, «+», ничего. */
sealed class GridHit {
    data class Cell(val track: Int, val row: Int) : GridHit()
    data class Scene(val row: Int) : GridHit()
    object Plus : GridHit()
    object None : GridHit()
}

private fun hit(g: GridGeom, d: GridData, x: Float, y: Float): GridHit {
    val r = (y / g.pitch).toInt()
    if (y < 0 || r >= d.windowRows || y - g.y(r) > g.rowH) return GridHit.None
    val k = d.rows[r]
    if (x < g.step) return if (k >= 0) GridHit.Scene(k) else if (d.plusRow == r) GridHit.Plus else GridHit.None
    val col = ((x - g.step - Layout.gap) / (g.trackW + Layout.gap)).toInt()
    if (col < 0 || col >= d.tracks || x - g.x(col) > g.trackW || k < 0) return GridHit.None
    return GridHit.Cell(col, k)
}

/** Касание пальца: куда, где и когда начато, сдвинуто ли, удержано, прокрутка колонки строк. */
private class GridPress(val hit: GridHit, val start: Offset, val t0: Long, val top0: Int) {
    var moved = false
    var held = false
    var scrolling = false
    var job: Job? = null
}

/** Сетка паттернов (PATTERNS; PatternGrid.swift): тап по ячейке — запуск, остановка, новый клип; удержание — правка без
 *  запуска, затем протяжка — копия клипа в другую ячейку; свайп вверх — меню ячейки; тап по строке — запуск строки,
 *  двойной тап — меню строки, удержание и протяжка — перенос строки, движение вверх-вниз — прокрутка окна строк.
 *  Рамка выделения, переход MIX и аранжировщик — позже. */
@Composable
fun PatternGrid(s: ScreenState, modifier: Modifier, width: Float) {
    val d = GridData(s.grid)
    BoxWithConstraints(modifier) {
        val g = GridGeom(maxWidth.value, maxHeight.value, Layout.step(width), d.tracks, d.windowRows)
        val ui = remember { GridUi() }
        val scope = rememberCoroutineScope()
        Box(Modifier.width((g.w - g.step - Layout.gap).dp).fillMaxHeight()
            .pointerInput(g.w, g.h, d.tracks) { gridTouches(s, g, ui, scope, density) }) {
            for (r in 0 until d.windowRows) {
                val k = d.rows[r]
                if (k >= 0) {
                    SceneCell(s, d, r, k, ui, Modifier.offset(0.dp, g.y(r).dp).size(g.step.dp, g.rowH.dp))
                    for (i in 0 until d.tracks) {
                        val lifted = ui.ghost != null && ((ui.dragCell == GridHit.Cell(i, k)) || ui.dragRow == k)
                        ClipCell(s, d, d.cells[r][i], i, ui.target == GridHit.Cell(i, k), lifted,
                            Modifier.offset(g.x(i).dp, g.y(r).dp).size(g.trackW.dp, g.rowH.dp))
                    }
                } else if (d.plusRow == r) {
                    PlusCell(Modifier.offset(0.dp, g.y(r).dp).size(g.step.dp, g.rowH.dp))
                }
            }
            Ghost(s, d, g, ui)
        }
        GridSide(s, d, g, Modifier.offset(g.sideX.dp, 0.dp).width(g.step.dp).fillMaxHeight(), ui)
        GridMenus(s, d, g, ui)
    }
}

/** Состояние жестов и меню сетки. */
class GridUi {
    var dragCell by mutableStateOf<GridHit.Cell?>(null)
    var dragRow by mutableStateOf<Int?>(null)
    var ghost by mutableStateOf<Offset?>(null)
    var target by mutableStateOf<GridHit.Cell?>(null)
    var rowTarget by mutableStateOf<Int?>(null)
    var cellMenu by mutableStateOf<GridHit.Cell?>(null)
    var rowMenu by mutableStateOf<Int?>(null)
    var gridMenu by mutableStateOf(false)
    var lastSceneTap: Pair<Int, Long>? = null
}

private const val HOLD_MS = 350L
private const val DOUBLE_TAP_MS = 300L

private suspend fun androidx.compose.ui.input.pointer.PointerInputScope.gridTouches(
    s: ScreenState, g: GridGeom, ui: GridUi, scope: CoroutineScope, dens: Float,
) {
    val presses = HashMap<PointerId, GridPress>()
    var dragId: PointerId? = null
    awaitPointerEventScope {
        while (true) {
            val ev = awaitPointerEvent()
            for (c in ev.changes) {
                val x = c.position.x / dens
                val y = c.position.y / dens
                val d = GridData(s.grid)
                if (c.pressed && !c.previousPressed) {
                    val p = GridPress(hit(g, d, x, y), Offset(x, y), System.currentTimeMillis(), d.top)
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
                            else -> {}
                        }
                    }
                } else if (c.pressed) {
                    val p = presses[c.id] ?: continue
                    val dx = x - p.start.x
                    val dy = y - p.start.y
                    if (hypot(dx, dy) > 8) p.moved = true
                    val h = p.hit
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
                        ui.dragCell = h
                        ui.ghost = Offset(x, y)
                        val t = hit(g, d, x, y)
                        ui.target = if (t is GridHit.Cell && t != h) t else null
                    }
                } else {
                    val p = presses.remove(c.id) ?: continue
                    p.job?.cancel()
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

/** Ячейка клипа: пустая — фон панели; заполненная — темнее, играющая — светлее и полоса хода внизу; открытая для
 *  правки — жёлтый контур; ожидающая — мигает. */
@Composable
private fun ClipCell(s: ScreenState, d: GridData, c: GridCell, track: Int, target: Boolean, lifted: Boolean, modifier: Modifier) {
    val th = LocalTheme.current
    val bg = if (!c.filled) th.panel else if (c.active) th.panel2 else th.panel2.copy(alpha = 0.62f)
    val outline = if (target) th.accent else if (c.editing) th.accent else Color.Transparent
    val blinkOff = c.queued && d.playing && !s.blink
    val shape = RoundedCornerShape(6.dp)
    Box(modifier.alpha(if (lifted) 0.35f else if (blinkOff) 0.35f else 1f).tile(bg, 6f).border(1.dp, outline, shape)) {
        if (c.filled) Thumb(c, Modifier.fillMaxSize().padding(start = 6.dp, end = 6.dp, top = 5.dp, bottom = 7.dp))
        if (c.active && d.playing) {
            val frac = s.gridLive.getOrElse(2 + track) { 0 } / 1000f
            ProgressBar(frac, Modifier.align(Alignment.BottomStart))
        }
    }
}

/** Миниатюра клипа: страницы строками по 16 шагов, по высоте — не меньше 4 строк, по центру. */
@Composable
private fun Thumb(c: GridCell, modifier: Modifier) {
    val th = LocalTheme.current
    Canvas(modifier) {
        val rows = c.pageCount
        val units = maxOf(rows, 4)
        val cw = size.width / 16
        val rh = size.height / units
        val top = (units - rows) / 2f * rh
        for (r in 0 until rows) {
            val bits = c.pages[2 * r]
            val out = c.pages[2 * r + 1]
            for (st in 0 until 16) {
                var color = th.off
                if (bits shr st and 1 != 0) color = th.text else if (bits shr (st + 16) and 1 != 0) color = th.text.copy(alpha = 0.45f)
                if (out shr st and 1 != 0) color = color.copy(alpha = color.alpha * 0.2f)
                drawRoundRect(color, Offset(st * cw + cw * 0.12f, top + r * rh + rh * 0.12f), Size(cw * 0.76f, rh * 0.76f),
                    CornerRadius(1.dp.toPx()))
            }
        }
    }
}

/** Жёлтая полоса хода внизу ячейки. */
@Composable
private fun ProgressBar(frac: Float, modifier: Modifier) {
    val th = LocalTheme.current
    BoxWithConstraints(modifier.fillMaxWidth().height(3.dp)) {
        Box(Modifier.width((maxWidth.value * frac.coerceIn(0f, 1f)).dp).fillMaxHeight().tile(th.accent, 0f))
    }
}

/** Ячейка строки (над MIX): имя и повторы (×n; у играющей — проход n/N или ∞ при LOOP ROW), полоса хода. */
@Composable
private fun SceneCell(s: ScreenState, d: GridData, r: Int, k: Int, ui: GridUi, modifier: Modifier) {
    val th = LocalTheme.current
    val filled = d.cells[r].any { it.filled }
    val playing = d.playing && d.sceneRow == k
    val reps = d.repeats[r]
    val n = s.gridLive.getOrElse(0) { 0 }
    val badge = if (playing) (if (d.rowLoop) "∞" else "${minOf(n + 1, reps)}/$reps") else "×$reps"
    val blinkOff = d.playing && d.sceneQueued == k && !s.blink
    val line = if (ui.dragRow != null && ui.rowTarget == k) th.accent else Color.Transparent
    val lifted = ui.ghost != null && ui.dragRow == k
    Box(modifier.alpha(if (blinkOff || lifted) 0.35f else 1f).framed(if (filled) th.panel2 else th.panel, line, 6f)) {
        Column(Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(2.dp, Alignment.CenterVertically)) {
            Text(s.sceneNames.getOrElse(k) { "${k + 1}" }, color = if (filled) th.text else th.off, fontSize = 12.sp,
                fontWeight = FontWeight.SemiBold, maxLines = 1)
            Text(badge, color = th.dim, fontSize = 10.sp)
        }
        if (playing) ProgressBar(s.gridLive.getOrElse(1) { 0 } / 1000f, Modifier.align(Alignment.BottomStart))
    }
}

/** «+» под последней строкой (в колонке строк). */
@Composable
private fun PlusCell(modifier: Modifier) {
    val th = LocalTheme.current
    Box(modifier.border(1.dp, th.line, RoundedCornerShape(6.dp)), contentAlignment = Alignment.Center) {
        Text("+", color = th.dim, fontSize = 20.sp)
    }
}

/** Поднятое пальцем: клип или строка целиком (колонка строк и клипы). */
@Composable
private fun Ghost(s: ScreenState, d: GridData, g: GridGeom, ui: GridUi) {
    val p = ui.ghost ?: return
    val c = ui.dragCell
    val k = ui.dragRow
    if (c != null) {
        val r = d.rows.indexOf(c.row)
        if (r < 0) return
        ClipCell(s, d, d.cells[r][c.track], c.track, false, false,
            Modifier.offset((p.x - g.trackW / 2).dp, (p.y - g.rowH / 2).dp).size(g.trackW.dp, g.rowH.dp)
                .shadow(10.dp, RoundedCornerShape(6.dp)))
    } else if (k != null) {
        val r = d.rows.indexOf(k)
        if (r < 0) return
        Box(Modifier.offset(0.dp, (p.y - g.rowH / 2).dp).shadow(10.dp, RoundedCornerShape(6.dp))) {
            SceneCell(s, d, r, k, GridUi(), Modifier.size(g.step.dp, g.rowH.dp))
            for (i in 0 until d.tracks)
                ClipCell(s, d, d.cells[r][i], i, false, false, Modifier.offset(g.x(i).dp, 0.dp).size(g.trackW.dp, g.rowH.dp))
        }
    }
}
