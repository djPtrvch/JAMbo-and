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
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.MidiTarget

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

/** Сетка паттернов (PATTERNS; PatternGrid.swift): тап по ячейке — запуск, остановка, новый клип; удержание — правка без
 *  запуска, затем протяжка — копия клипа в другую ячейку; свайп вверх — меню ячейки; тап по строке — запуск строки,
 *  двойной тап — меню строки, удержание и протяжка — перенос строки, движение вверх-вниз — прокрутка окна строк.
 *  Рамка выделения, переход MIX и аранжировщик — позже. */
@Composable
fun PatternGrid(s: ScreenState, modifier: Modifier, width: Float) {
    val d = GridData(s.grid)
    val phone = LocalConfiguration.current.smallestScreenWidthDp < 600
    BoxWithConstraints(modifier) {
        val g = GridGeom(maxWidth.value, maxHeight.value, Layout.step(width), d.tracks, d.windowRows)
        val ui = remember { GridUi() }
        val scope = rememberCoroutineScope()
        Box(Modifier.width((g.w - g.step - Layout.gap).dp).fillMaxHeight()
            .onGloballyPositioned { s.patternTouchArea = it }
            .pointerInput(g.w, g.h, d.tracks, phone) { gridTouches(s, g, ui, scope, density, phone) }) {
            for (r in 0 until d.windowRows) {
                val k = d.rows[r]
                if (k >= 0) {
                    SceneCell(s, d, r, k, ui, Modifier.offset(0.dp, g.y(r).dp).size(g.step.dp, g.rowH.dp)
                        .midiLearnMark(s, MidiTarget.ROW + r, 6f))
                    for (i in 0 until d.tracks) {
                        val cell = GridHit.Cell(i, k)
                        val lifted = ui.ghost != null && (ui.dragCell == cell || ui.dragRow == k ||
                            ui.dragCell?.let { ui.selection.group(it) && ui.selection.contains(cell) } == true)
                        ClipCell(s, d, d.cells[r][i], i, ui.target == GridHit.Cell(i, k), lifted,
                            Modifier.offset(g.x(i).dp, g.y(r).dp).size(g.trackW.dp, g.rowH.dp)
                                .midiLearnMark(s, MidiTarget.clip(i, r), 6f),
                            d.cells[r][i].filled && ui.selection.contains(cell))
                    }
                } else if (d.plusRow == r) {
                    PlusCell(Modifier.offset(0.dp, g.y(r).dp).size(g.step.dp, g.rowH.dp))
                }
            }
            val ink = LocalTheme.current.text
            ui.band?.let { rect ->
                Canvas(Modifier.fillMaxSize()) {
                    val start = Offset(rect.left.dp.toPx(), rect.top.dp.toPx())
                    val bounds = Size(rect.width.dp.toPx(), rect.height.dp.toPx())
                    drawRect(ink.copy(alpha = 0.13f), start, bounds)
                    drawRect(ink, start, bounds, style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
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
    internal var selection by mutableStateOf(GridSelection())
    internal var band by mutableStateOf<Rect?>(null)
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

/** Ячейка клипа: пустая — фон панели; заполненная — темнее, играющая — светлее и полоса хода внизу; открытая для
 *  правки — жёлтый контур; ожидающая — мигает. */
@Composable
private fun ClipCell(s: ScreenState, d: GridData, c: GridCell, track: Int, target: Boolean, lifted: Boolean, modifier: Modifier, selected: Boolean = false) {
    val th = LocalTheme.current
    val bg = if (!c.filled) th.panel else if (c.active) th.panel2 else th.panel2.copy(alpha = 0.62f)
    val outline = if (selected) th.text else if (target || c.editing) th.accent else Color.Transparent
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
    val line = if (k in ui.selection.rows) th.text else if (ui.dragRow != null && ui.rowTarget == k) th.accent else Color.Transparent
    val lifted = ui.ghost != null && ui.dragRow == k
    Box(modifier.alpha(if (blinkOff || lifted) 0.35f else 1f).framed(if (filled) th.panel2 else th.panel, line, 6f)) {
        Column(Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(2.dp, Alignment.CenterVertically)) {
            Text(s.sceneNames.getOrElse(k) { "${k + 1}" }, color = if (filled) th.text else th.off, fontSize = 12.tsp(),
                fontWeight = FontWeight.SemiBold, maxLines = 1)
            Text(badge, color = th.dim, fontSize = 10.tsp())
        }
        if (playing) ProgressBar(s.gridLive.getOrElse(1) { 0 } / 1000f, Modifier.align(Alignment.BottomStart))
    }
}

/** «+» под последней строкой (в колонке строк). */
@Composable
private fun PlusCell(modifier: Modifier) {
    val th = LocalTheme.current
    Box(modifier.border(1.dp, th.line, RoundedCornerShape(6.dp)), contentAlignment = Alignment.Center) {
        Text("+", color = th.dim, fontSize = 20.isp())
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
        if (ui.selection.group(c)) {
            val dx = p.x - g.x(c.track) - g.trackW / 2
            val dy = p.y - g.y(r) - g.rowH / 2
            for ((rowIndex, row) in d.rows.withIndex()) {
                if (row < 0) continue
                for (track in 0 until d.tracks) {
                    val cell = d.cells[rowIndex][track]
                    if (!cell.filled || !ui.selection.contains(GridHit.Cell(track, row))) continue
                    ClipCell(s, d, cell, track, false, false,
                        Modifier.offset((g.x(track) + dx).dp, (g.y(rowIndex) + dy).dp)
                            .size(g.trackW.dp, g.rowH.dp).shadow(10.dp, RoundedCornerShape(6.dp)), selected = true)
                }
            }
            return
        }
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
