package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import app.jambo.android.AppNative
import app.jambo.android.MasterWinAct
import kotlinx.coroutines.delay
import kotlin.math.abs

/** Модули окна: SAT 0, COMP 1, CLIP 2, LIM 3. */
private val titles = listOf("SAT", "COMP", "CLIP", "LIM")

/** Что открыто поверх окна модуля: меню модулей, меню пресетов, ввод числом, имя пресета, имя занято. */
class MasterUi {
    var menu by mutableStateOf(0)  // 0 — нет, 1 — модули, 2 — пресеты
    var anchor by mutableStateOf(Rect.Zero)
    var input by mutableStateOf<String?>(null)  // id параметра
    var save by mutableStateOf(false)
    var rename by mutableStateOf(-1)
    var clash by mutableStateOf<Pair<String, Int>?>(null)  // имя, занятый пресет (−1 — переименование)
}

/**
 * Окно модуля мастера (MasterWindow.swift ветки swift JAMbo) на месте центра при MST: вверху строка — название (тап —
 * меню SAT, COMP, CLIP, LIM), поля параметров, у CLIP и LIM — UNITY, PRESETS, BYPASS; ниже — дисплей модуля. Закрывает
 * двойной тап по MST (и EQ, PATTERNS, браузер).
 */
@Composable
fun MasterWindow(s: ScreenState, modifier: Modifier) {
    val ui = remember { MasterUi() }
    val m = s.masterWin[0]
    Column(modifier, verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        MasterStrip(s, ui, m, Modifier.fillMaxWidth().height(48.dp))
        MasterDisplay(s, m, Modifier.fillMaxWidth().weight(1f))
    }
    MasterOverlays(s, ui, m)
}

@Composable
private fun MasterStrip(s: ScreenState, ui: MasterUi, m: Int, modifier: Modifier) {
    val th = LocalTheme.current
    val d = LocalDensity.current.density
    val n = s.masterWin[3]
    val t = s.masterWinTexts
    val bypass = s.masterWin[2] != 0
    // Первые поля — до черты над правым краем TRANSFER (у COMP — 4, у SAT и CLIP — 2; у LIM черты нет).
    val lead = if (m == 1) 4 else 2
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        Box(Modifier.width(52.dp).fillMaxHeight().onGloballyPositioned { c -> s.body?.let { b -> if (b.isAttached) ui.anchor = b.localBoundingBoxOf(c) } }
            .pointerTap { ui.menu = 1 }, contentAlignment = Alignment.Center) {
            Text("MASTER\n${titles.getOrElse(m) { "" }} ▾", color = th.dim, fontSize = 11.tsp(), letterSpacing = 0.9.sp,
                textAlign = TextAlign.Center, maxLines = 2)
        }
        for (i in 0 until n) {
            if (3 * i + 2 >= t.size) break
            if (i == lead && m != 3) Box(Modifier.width(1.dp).fillMaxHeight().padding(vertical = 8.dp).background(th.line))
            val id = t[3 * i]
            // У LIM при BYPASS серые только INPUT и CEILING.
            val gray = bypass && (m != 3 || id == "lim.input" || id == "lim.ceiling")
            MasterField(s, ui, id, t[3 * i + 1], t[3 * i + 2], gray, Modifier.weight(1f).fillMaxHeight())
        }
        if (m == 2 || m == 3) ToggleField("UNITY", s.masterWin[1] != 0, Modifier.width(76.dp).fillMaxHeight()) {
            AppNative.masterWinAct(MasterWinAct.UNITY, 0, 0, ""); s.refresh()
        }
        StripTool("PRESETS", ui.menu == 2, Modifier.width(Layout.topButton.dp).fillMaxHeight()) { ui.menu = 2 }
        StripTool("BYPASS", bypass, Modifier.width(Layout.topButton.dp).fillMaxHeight()) {
            AppNative.masterAct(6, m); s.refresh()
        }
    }
}

/** Поле окна модуля: подпись над значением; палец вверх-вниз — 240 точек на весь диапазон (одна запись UNDO);
 *  двойной тап — ввод числом; тап — TYPE, RATIO, RELEASE, MAKEUP (если второго тапа нет за 0,33 с). */
@Composable
private fun MasterField(s: ScreenState, ui: MasterUi, id: String, label: String, value: String, gray: Boolean, modifier: Modifier) {
    val th = LocalTheme.current
    var active by remember { mutableStateOf(false) }
    var lastTap by remember { mutableStateOf(0L) }
    var pending by remember { mutableStateOf(0L) }
    val cur by rememberUpdatedState(id)
    LaunchedEffect(pending) {
        if (pending == 0L) return@LaunchedEffect
        delay(330)
        AppNative.masterParam(cur, 5, 0.0)
        s.refresh()
        pending = 0
    }
    Column(modifier.tile(if (active) th.panel2 else th.panel).padding(horizontal = 12.dp).pointerInput(id) {
        awaitEachGesture {
            val down = awaitFirstDown()
            active = true
            AppNative.act(Act.BEGIN_GESTURE, 0, 0, 0.0)
            val start = AppNative.masterParam(cur, 1, 0.0)
            var moved = false
            while (true) {
                val ev = awaitPointerEvent()
                val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                val dy = (c.position.y - down.position.y) / density
                if (!moved && abs(dy) > 10) moved = true
                if (moved) {
                    AppNative.masterParam(cur, 4, start - dy / 240.0)
                    s.refresh()
                }
                c.consume()
                if (!c.pressed) break
            }
            AppNative.act(Act.END_GESTURE, 0, 0, 0.0)
            active = false
            if (!moved) {
                val now = System.currentTimeMillis()
                if (now - lastTap < 320) {
                    lastTap = 0
                    pending = 0
                    ui.input = cur
                } else {
                    lastTap = now
                    if (cur in setOf("cmp.ratio", "sat.type", "cmp.release", "lim.release", "cmp.makeup")) pending = now
                }
            }
        }
    }, verticalArrangement = Arrangement.Center) {
        Text(label, color = th.dim, fontSize = 9.tsp(), letterSpacing = 0.8.sp, maxLines = 1)
        Text(value, color = if (gray) th.off else th.text, fontSize = 16.tsp(), maxLines = 1)
    }
}

/** Вкл./выкл. в виде поля (UNITY): подпись и ON / OFF, тап переключает. */
@Composable
private fun ToggleField(title: String, on: Boolean, modifier: Modifier, action: () -> Unit) {
    val th = LocalTheme.current
    Column(modifier.tile(th.panel).padding(horizontal = 12.dp).pointerTap(action), verticalArrangement = Arrangement.Center) {
        Text(title, color = th.dim, fontSize = 9.tsp(), letterSpacing = 0.8.sp, maxLines = 1)
        Text(if (on) "ON" else "OFF", color = if (on) th.accentText else th.text, fontSize = 16.tsp(), maxLines = 1)
    }
}

@Composable
private fun StripTool(title: String, active: Boolean, modifier: Modifier, action: () -> Unit) {
    val th = LocalTheme.current
    Box(modifier.tile(if (active) th.accent else th.panel2).pointerTap(action), contentAlignment = Alignment.Center) {
        Text(title, color = if (active) th.onAccent else th.text, fontSize = 12.tsp(), fontWeight = FontWeight.Bold,
            letterSpacing = 0.8.sp, maxLines = 1)
    }
}

/** Меню модулей и пресетов, ввод числом, имя пресета, вопрос «имя занято». */
@Composable
private fun MasterOverlays(s: ScreenState, ui: MasterUi, m: Int) {
    val th = LocalTheme.current
    val d = LocalDensity.current.density
    val t = s.masterWinTexts
    val presets = t.drop(3 * s.masterWin[3])
    if (ui.menu != 0) {
        Box(Modifier.fillMaxSize().pointerTap { ui.menu = 0 })
        val x = ui.anchor.left / d
        val y = ui.anchor.bottom / d + 6
        val shape = RoundedCornerShape(Layout.radius.dp)
        val box = Modifier.offset(x.dp, y.dp).shadow(12.dp, shape).background(th.bg, shape).border(1.dp, th.line, shape).padding(8.dp)
        if (ui.menu == 1) Column(box.width(150.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            for (k in 0 until 4) MenuItem(titles[k], current = k == m) {
                ui.menu = 0
                AppNative.masterWinAct(MasterWinAct.OPEN, k, 0, ""); s.refresh()
            }
        } else Column(box.width(260.dp).heightIn(max = 420.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            MenuHead("${titles.getOrElse(m) { "" }} PRESETS · ${presets.size}")
            MenuItem("SAVE") { ui.menu = 0; ui.save = true }
            val saved = s.masterWin[5] != 0
            MenuItem(if (saved) "SAVED" else "SAVE DEFAULT", current = saved) {
                AppNative.masterWinAct(MasterWinAct.SAVE_DEFAULT, 0, 0, ""); s.refresh()
            }
            if (presets.isEmpty()) MenuNote("No ${titles.getOrElse(m) { "" }} presets yet — save one with SAVE")
            else Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                var open by remember { mutableStateOf(-1) }
                presets.forEachIndexed { k, name ->
                    SwipeRow(name, null, null, null, false, open == k, { o -> open = if (o) k else -1 }, {
                        ui.menu = 0
                        AppNative.masterWinAct(MasterWinAct.PRESET_LOAD, k, 0, ""); s.refresh()
                    }, { ui.menu = 0; ui.rename = k }) {
                        AppNative.masterWinAct(MasterWinAct.PRESET_DELETE, k, 0, ""); s.refresh()
                    }
                }
            }
        }
    }
    ui.input?.let { id ->
        val label = t.indices.firstOrNull { it % 3 == 0 && t[it] == id && it < 3 * s.masterWin[3] }?.let { t[it + 1] } ?: ""
        InputDialog(label, AppNative.masterInput(id, ""), 16, { ui.input = null }) { text ->
            ui.input = null
            AppNative.masterInput(id, text); s.refresh()
        }
    }
    if (ui.save) InputDialog("PRESET NAME", AppNative.masterPresetNextName(), 40, { ui.save = false }) { text ->
        ui.save = false
        val k = AppNative.masterPresetIndex(text, -1)
        if (k >= 0) ui.clash = text to k else { AppNative.masterWinAct(MasterWinAct.PRESET_SAVE, -1, 0, text); s.refresh() }
    }
    if (ui.rename >= 0) InputDialog("RENAME", presets.getOrElse(ui.rename) { "" }, 40, { ui.rename = -1 }) { text ->
        val k = ui.rename
        ui.rename = -1
        if (AppNative.masterPresetIndex(text, k) >= 0) ui.clash = text to -1 - k
        else { AppNative.masterWinAct(MasterWinAct.PRESET_RENAME, k, 0, text); s.refresh() }
    }
    ui.clash?.let { (name, k) ->
        val buttons = if (k >= 0) listOf(
            Triple("OVERWRITE", true) { ui.clash = null; AppNative.masterWinAct(MasterWinAct.PRESET_SAVE, k, 0, name); s.refresh() },
            Triple("SAVE AS…", true) { ui.clash = null; ui.save = true },
            Triple("CANCEL", true) { ui.clash = null },
        ) else listOf(
            Triple("RENAME…", true) { ui.clash = null; ui.rename = -1 - k },
            Triple("CANCEL", true) { ui.clash = null },
        )
        ChoiceDialog("“$name” already exists", null, buttons) { ui.clash = null }
    }
}
