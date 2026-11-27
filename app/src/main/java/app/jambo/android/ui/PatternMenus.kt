package app.jambo.android.ui

import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import app.jambo.android.GridOpt
import app.jambo.android.MidiTarget

/** Колонка справа (над MST): бутерброд сетки, LOOP ROW, стрелки окна строк (если строк больше, чем в окне). */
@Composable
fun GridSide(s: ScreenState, d: GridData, g: GridGeom, modifier: Modifier, ui: GridUi) {
    val th = LocalTheme.current
    Column(modifier, verticalArrangement = Arrangement.spacedBy(GridGeom.ROW_GAP.dp)) {
        Box(Modifier.fillMaxWidth().height(g.rowH.dp).pointerTap { ui.gridMenu = true }, contentAlignment = Alignment.Center) {
            Burger(th.text)
        }
        val loop = d.rowLoop
        Box(Modifier.fillMaxWidth().height(g.rowH.dp).tile(if (loop) th.accent else th.panel2, 6f)
            .pointerTap { s.act(Act.SET_GRID_OPTION, GridOpt.ROW_LOOP) }, contentAlignment = Alignment.Center) {
            LoopIcon(if (loop) th.onAccent else th.text)
        }
        if (d.maxTop > 0 || s.midi.controls.learning) {
            val sw = d.arrowSwitch && d.playing
            Arrow(s, "▲", d.top > 0 || sw, g, MidiTarget.GRID_UP) {
                if (!s.learnTarget(MidiTarget.GRID_UP)) s.act(Act.GRID_ARROW, -1)
            }
            Arrow(s, "▼", d.top < d.maxTop || sw, g, MidiTarget.GRID_DOWN) {
                if (!s.learnTarget(MidiTarget.GRID_DOWN)) s.act(Act.GRID_ARROW, 1)
            }
        }
        val recording = s.arrange.getOrElse(2) { 0 } != 0
        val armed = s.arrange.getOrElse(1) { 0 } != 0
        Box(Modifier.fillMaxWidth().height(g.rowH.dp)
            .tile(if (armed) th.rec.copy(alpha = if (recording) 0.8f else 0.42f) else th.panel2, 6f)
            .pointerTap { s.arrangeAct(ArrangeAct.RECORD) }, contentAlignment = Alignment.Center) {
            Text("● ARR", color = th.text, fontSize = 12.tsp())
        }
    }
}

@Composable
private fun Arrow(s: ScreenState, t: String, on: Boolean, g: GridGeom, target: Int, action: () -> Unit) {
    val th = LocalTheme.current
    Box(Modifier.fillMaxWidth().height(g.rowH.dp).midiLearnMark(s, target, 6f).tile(th.panel2, 6f).pointerTap(action), contentAlignment = Alignment.Center) {
        Text(t, color = if (on) th.text else th.text.copy(alpha = 0.35f), fontSize = 16.isp())
    }
}

/** Меню сетки: ячейки (COPY, PASTE, DELETE), строки (имя, повторы, DUPLICATE, COPY, PASTE, DELETE, OK), бутерброд
 *  (START FROM ROW, AT THE END, + ADDS, ▲ ▼ ARROWS). Тап мимо — закрыть. TO ARRANGE — с аранжировщиком. */
@Composable
fun GridMenus(s: ScreenState, d: GridData, g: GridGeom, ui: GridUi) {
    val menuW = 2 * g.trackW + Layout.gap
    ui.cellMenu?.let { c ->
        Dismiss { ui.cellMenu = null }
        val r = maxOf(0, d.rows.indexOf(c.row))
        val w = 180f
        val h = 4 * 40f + 3 * 6f + 16
        val x = minOf(maxOf(0f, g.x(c.track) + g.trackW / 2 - w / 2), g.w - w)
        val below = g.y(r) + g.rowH + 6
        val y = if (below + h < g.h) below else maxOf(0f, g.y(r) - 6 - h)
        val filled = d.cells.getOrNull(r)?.getOrNull(c.track)?.filled ?: false
        val run = { code: Int -> ui.cellMenu = null; s.act(code, c.track, c.row); Unit }
        Panel(Modifier.offset(x.dp, y.dp).width(w.dp)) {
            MenuItem("COPY", enabled = filled) { run(Act.COPY_CLIP) }
            MenuItem("PASTE", enabled = d.hasClipCopy) { run(Act.PASTE_CLIP) }
            MenuItem("DELETE", enabled = filled, red = true) { run(Act.DELETE_CLIP) }
            MenuItem("TO ARRANGE", enabled = filled) {
                if (ui.selection.group(c)) {
                    s.sendGridSelection(ui.selection.encode())
                    ui.selection = GridSelection()
                } else s.arrangeAct(ArrangeAct.CLIP_TO_ARRANGE, c.track, c.row)
                ui.cellMenu = null
            }
        }
    }
    ui.rowMenu?.let { k ->
        Dismiss { ui.rowMenu = null }
        val r = maxOf(0, d.rows.indexOf(k))
        val h = 310f
        val below = g.y(r) + g.rowH + 6
        val y = if (below + h < g.h) below else maxOf(0f, g.y(r) - 6 - h)
        RowMenu(s, d, k, ui, Modifier.offset(g.x(0).dp, y.dp).width(menuW.dp)) { ui.rowMenu = null }
    }
    if (ui.gridMenu) {
        Dismiss { ui.gridMenu = false }
        val right = g.x(maxOf(0, d.tracks - 1)) + g.trackW
        val w = minOf(g.w, menuW)
        GridMenu(s, d, Modifier.offset(maxOf(0f, right - w).dp, 0.dp).width(w.dp).height(g.h.dp))
    }
}

@Composable
private fun Dismiss(close: () -> Unit) {
    Box(Modifier.fillMaxSize().pointerTap(close))
}

@Composable
private fun Panel(modifier: Modifier, content: @Composable () -> Unit) {
    Column(modifier.menuBox().pointerInput(Unit) { detectTapGestures { } }.padding(8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)) { content() }
}

/** Меню строки: имя (до 12 знаков), REPEATS − n +, DUPLICATE, COPY, PASTE, DELETE, OK (имя и повторы — по OK). */
@Composable
private fun RowMenu(s: ScreenState, d: GridData, k: Int, ui: GridUi, modifier: Modifier, close: () -> Unit) {
    val th = LocalTheme.current
    val names = s.sceneNames
    val n = d.visibleRows
    var name by remember(k) { mutableStateOf(names.getOrElse(n + k) { "" }) }
    var reps by remember(k) { mutableIntStateOf(d.repeats.getOrElse(maxOf(0, d.rows.indexOf(k))) { 1 }) }
    val act = { code: Int -> close(); s.act(code, k); Unit }
    Panel(modifier) {
        BasicTextField(name, { if (it.length <= 12 || it.length <= name.length) name = it }, singleLine = true,
            textStyle = TextStyle(color = th.text, fontSize = 16.tsp()), cursorBrush = SolidColor(th.text),
            modifier = Modifier.fillMaxWidth().height(44.dp).tile(th.panel, 6f).border(1.dp, th.text, RoundedCornerShape(6.dp))
                .padding(horizontal = 10.dp, vertical = 12.dp))
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Text("REPEATS", color = th.dim, fontSize = 10.tsp(), letterSpacing = 0.8.sp, modifier = Modifier.weight(1f))
            MenuItem("−", Modifier.width(44.dp)) { reps = maxOf(1, reps - 1) }
            Text("×$reps", color = th.text, fontSize = 17.tsp(), modifier = Modifier.width(44.dp))
            MenuItem("+", Modifier.width(44.dp)) { reps = minOf(64, reps + 1) }
        }
        MenuChoices(listOf("DUPLICATE" to false, "COPY" to false), enabled = listOf(d.canAddRow, true)) {
            act(if (it == 0) Act.DUPLICATE_ROW else Act.COPY_ROW)
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            MenuItem("PASTE", Modifier.weight(1f), enabled = d.hasRowCopy) { act(Act.PASTE_ROW) }
            MenuItem("DELETE", Modifier.weight(1f), red = true) { act(Act.DELETE_ROW) }
        }
        MenuItem("TO ARRANGE") {
            s.act(Act.BEGIN_GESTURE)
            try {
                AppNative.setScene(k, name, reps)
                if (ui.selection.touches(k)) {
                    s.sendGridSelection(ui.selection.encode())
                    ui.selection = GridSelection()
                } else s.arrangeAct(ArrangeAct.ROW, k, -1)
            } finally {
                s.act(Act.END_GESTURE)
            }
            close()
        }
        MenuItem("OK") {
            AppNative.setScene(k, name, reps)
            s.refresh()
            close()
        }
    }
}

/** Бутерброд сетки: строка старта, что в конце строк, что делает «+», режим стрелок. */
@Composable
private fun GridMenu(s: ScreenState, d: GridData, modifier: Modifier) {
    Column(modifier.menuBox().pointerInput(Unit) { detectTapGestures { } }.padding(8.dp)
        .verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        MenuHead("START FROM ROW")
        for (r in 0 until (d.visibleRows + 3) / 4) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (c in 0 until 4) {
                    val k = r * 4 + c
                    if (k < d.visibleRows) {
                        MenuItem(s.sceneNames.getOrElse(k) { "${k + 1}" }, Modifier.weight(1f), current = k == d.start) {
                            s.act(Act.SET_GRID_OPTION, GridOpt.START, k)
                        }
                    } else Box(Modifier.weight(1f))
                }
            }
        }
        MenuHead("AT THE END")
        MenuChoices(listOf("STOP" to d.endStop, "LOOP" to !d.endStop)) { s.act(Act.SET_GRID_OPTION, GridOpt.END_STOP, 1 - it) }
        MenuHead("+ ADDS")
        MenuChoices(listOf("NEW PATTERNS" to !d.plusCopy, "COPY PLAYING" to d.plusCopy)) { s.act(Act.SET_GRID_OPTION, GridOpt.PLUS_COPY, it) }
        MenuHead("▲ ▼ ARROWS")
        MenuChoices(listOf("VIEW" to !d.arrowSwitch, "SWITCH" to d.arrowSwitch)) { s.act(Act.SET_GRID_OPTION, GridOpt.ARROW_SWITCH, it) }
    }
}

/** Значок LOOP ROW (как «повтор»): прямоугольная петля со скруглением, вверху стрелка вправо, внизу — влево. */
@Composable
internal fun LoopIcon(color: androidx.compose.ui.graphics.Color) {
    val k = LocalIconScale.current
    androidx.compose.foundation.Canvas(Modifier.size((18 * k).dp, (13 * k).dp)) {
        val w = size.width
        val h = size.height
        val sw = 1.8f * density * k
        val r = h * 0.32f
        val m = sw
        val st = androidx.compose.ui.graphics.drawscope.Stroke(sw, cap = androidx.compose.ui.graphics.StrokeCap.Round,
            join = androidx.compose.ui.graphics.StrokeJoin.Round)
        val top = androidx.compose.ui.graphics.Path().apply {
            moveTo(m, h * 0.62f)
            lineTo(m, r + m)
            quadraticTo(m, m, r + m, m)
            lineTo(w - m - h * 0.2f, m)
        }
        val bottom = androidx.compose.ui.graphics.Path().apply {
            moveTo(w - m, h * 0.38f)
            lineTo(w - m, h - r - m)
            quadraticTo(w - m, h - m, w - r - m, h - m)
            lineTo(m + h * 0.2f, h - m)
        }
        drawPath(top, color, style = st)
        drawPath(bottom, color, style = st)
        val a = h * 0.26f
        val arrow1 = androidx.compose.ui.graphics.Path().apply {
            moveTo(w - m - h * 0.2f - a, m - a)
            lineTo(w - m - h * 0.2f + sw * 0.3f, m)
            lineTo(w - m - h * 0.2f - a, m + a)
        }
        val arrow2 = androidx.compose.ui.graphics.Path().apply {
            moveTo(m + h * 0.2f + a, h - m - a)
            lineTo(m + h * 0.2f - sw * 0.3f, h - m)
            lineTo(m + h * 0.2f + a, h - m + a)
        }
        drawPath(arrow1, color, style = st)
        drawPath(arrow2, color, style = st)
    }
}
