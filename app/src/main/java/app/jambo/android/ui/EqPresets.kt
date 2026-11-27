package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative

/**
 * Меню PRESETS EQ (EqPresetsMenu.swift ветки swift JAMbo): SAVE (имя — окном ввода), SAVE DEFAULT — только у EQ
 * мастера, список: тап — загрузить в EQ показанного канала (UNDO), свайп влево — RENAME и DELETE, удержание и
 * перетаскивание — порядок. Правым краем к кнопке PRESETS; тап мимо — закрыть.
 */
@Composable
fun EqPresetsOverlay(s: ScreenState) {
    val th = LocalTheme.current
    val d = LocalDensity.current.density
    var save by remember { mutableStateOf(false) }
    var rename by remember { mutableStateOf(-1) }
    var clash by remember { mutableStateOf<Pair<String, Int>?>(null) }
    val names = s.eqPresetNames
    if (s.eqPresetsOpen) {
        val r = s.topRects["patterns"]
        Box(Modifier.fillMaxSize().pointerTap { s.eqPresetsOpen = false })
        val x = maxOf(8f, (r?.right ?: 0f) / d - 260)
        val y = (r?.bottom ?: 0f) / d + 48 + 12
        val shape = RoundedCornerShape(Layout.radius.dp)
        Column(Modifier.offset(x.dp, y.dp).width(260.dp).heightIn(max = 460.dp).shadow(12.dp, shape).background(th.bg, shape)
            .border(1.dp, th.line, shape).padding(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            MenuHead("EQ PRESETS")
            MenuItem("SAVE") { s.eqPresetsOpen = false; save = true }
            if (s.eqExtra[2] != 0) {
                val saved = s.eqExtra[1] != 0
                MenuItem(if (saved) "SAVED" else "SAVE DEFAULT", current = saved) { AppNative.eqPresetAct(6, 0, 0, ""); s.refresh() }
            }
            if (names.isEmpty()) MenuNote("No EQ presets yet — save one with SAVE")
            else PresetRows(
                names.asList(),
                load = { k ->
                    s.eqPresetsOpen = false
                    AppNative.eqPresetAct(2, k, 0, ""); s.refresh()
                },
                rename = { k -> s.eqPresetsOpen = false; rename = k },
                delete = { k -> AppNative.eqPresetAct(4, k, 0, ""); s.refresh() },
                move = { from, to -> AppNative.eqPresetAct(5, from, to, ""); s.refresh() },
            )
        }
    }
    if (save) InputDialog("PRESET NAME", AppNative.eqPresetNextName(), 40, { save = false }) { text ->
        save = false
        val k = AppNative.eqPresetIndex(text, -1)
        if (k >= 0) clash = text to k else { AppNative.eqPresetAct(1, -1, 0, text); s.refresh() }
    }
    if (rename >= 0) InputDialog("RENAME", names.getOrElse(rename) { "" }, 40, { rename = -1 }) { text ->
        val k = rename
        rename = -1
        if (AppNative.eqPresetIndex(text, k) >= 0) clash = text to -1 - k
        else { AppNative.eqPresetAct(3, k, 0, text); s.refresh() }
    }
    clash?.let { (name, k) ->
        val buttons = if (k >= 0) listOf(
            Triple("OVERWRITE", true) { clash = null; AppNative.eqPresetAct(1, k, 0, name); s.refresh() },
            Triple("SAVE AS…", true) { clash = null; save = true },
            Triple("CANCEL", true) { clash = null },
        ) else listOf(
            Triple("RENAME…", true) { clash = null; rename = -1 - k },
            Triple("CANCEL", true) { clash = null },
        )
        ChoiceDialog("“$name” already exists", null, buttons) { clash = null }
    }
}
