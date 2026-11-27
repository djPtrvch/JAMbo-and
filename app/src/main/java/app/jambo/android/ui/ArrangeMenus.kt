package app.jambo.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import app.jambo.android.AppNative

@Composable
internal fun ArrangeMenus(s: ScreenState, d: ArrangeData, menu: ArrangeMenu, sections: Array<String>,
    groupAction: (Int) -> Unit, close: () -> Unit) {
    val theme = LocalTheme.current
    fun action(command: Int, a: Int = 0, b: Int = 0, c: Int = 0) {
        s.arrangeAct(command, a, b, c)
        close()
    }

    Dialog(onDismissRequest = close) {
        Column(Modifier.width(340.dp).fillMaxHeight(0.74f).menuBox().padding(8.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(6.dp)) {
            when (menu) {
                ArrangeMenu.Grid -> {
                    MenuHead("GRID")
                    listOf("1 BAR", "1/2", "1/4", "1 STEP").forEachIndexed { i, title ->
                        MenuItem(title, current = d.grid == i) { action(ArrangeAct.GRID, i) }
                    }
                    MenuItem("SNAP TO GRID", current = d.snap) { action(ArrangeAct.SNAP) }
                }
                is ArrangeMenu.Block -> {
                    val block = d.tracks.getOrNull(menu.track)?.blocks?.getOrNull(menu.block)
                    MenuHead(block?.let { s.sceneNames.getOrNull(it.clip).takeUnless { it.isNullOrEmpty() } ?: (it.clip + 1).toString() } ?: "BLOCK")
                    MenuItem("COPY") { action(ArrangeAct.COPY_BLOCK, menu.track, menu.block) }
                    MenuItem("PASTE", enabled = d.hasBlockCopy) { action(ArrangeAct.PASTE_BLOCK, menu.track, menu.block) }
                    MenuItem("DUPLICATE") { action(ArrangeAct.DUPLICATE, menu.track, menu.block) }
                    MenuItem("DELETE", red = true) { action(ArrangeAct.REMOVE, menu.track, menu.block) }
                }
                is ArrangeMenu.Lane -> {
                    val clips = remember(menu.track, s.revision) { AppNative.arrangeClips(menu.track) }
                    MenuHead("PATTERNS")
                    MenuItem("PASTE", enabled = d.hasBlockCopy) { action(ArrangeAct.PASTE_BLOCK, menu.track, -1, menu.step) }
                    for (clip in clips) {
                        val name = s.sceneNames.getOrNull(clip).takeUnless { it.isNullOrEmpty() } ?: (clip + 1).toString()
                        MenuItem(name) { action(ArrangeAct.ADD, menu.track, clip, menu.step) }
                    }
                }
                is ArrangeMenu.Track -> {
                    MenuHead(s.trackNames.getOrNull(menu.track) ?: "TRACK")
                    MenuItem("COPY") { action(ArrangeAct.TRACK_CLIPBOARD, menu.track, 0) }
                    MenuItem("PASTE", enabled = d.hasTrackCopy) { action(ArrangeAct.TRACK_CLIPBOARD, menu.track, 1) }
                    MenuItem("DUPLICATE") { action(ArrangeAct.TRACK_CLIPBOARD, menu.track, 2) }
                    val palette = arrangeColors.indices.filter { it !in setOf(3, 16, 17) }
                    palette.chunked(5).forEach { row ->
                        Row(Modifier.fillMaxWidth().height(42.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            for (color in row) {
                                androidx.compose.foundation.layout.Box(Modifier.weight(1f).height(42.dp)
                                    .tile(arrangeColor(color)).pointerTap { action(ArrangeAct.COLOR, menu.track, color) })
                            }
                        }
                    }
                }
                is ArrangeMenu.Ruler -> {
                    val section = menu.section
                    if (section == null) {
                        MenuItem("ADD SECTION") { action(ArrangeAct.SECTION, menu.step) }
                    } else {
                        var title by remember(section) { mutableStateOf(sections.getOrElse(section) { "" }) }
                        BasicTextField(title, { if (it.length <= 24) title = it }, singleLine = true,
                            cursorBrush = SolidColor(theme.text), textStyle = TextStyle(color = theme.text, fontSize = 16.tsp()),
                            modifier = Modifier.fillMaxWidth().height(44.dp).tile(theme.panel).padding(10.dp))
                        MenuItem("RENAME") {
                            AppNative.arrangeRenameSection(section, title)
                            s.refresh()
                            close()
                        }
                        MenuItem("DELETE SECTION", red = true) { action(ArrangeAct.DELETE_SECTION, section) }
                    }
                    MenuItem("CURSOR HERE") { action(ArrangeAct.SEEK, menu.step) }
                }
                ArrangeMenu.Group -> {
                    MenuItem("DUPLICATE") { groupAction(1); close() }
                    MenuItem("DELETE", red = true) { groupAction(2); close() }
                }
                is ArrangeMenu.Drop -> {
                    listOf("HERE", "INSERT", "TO END").forEachIndexed { mode, label ->
                        MenuItem(label) {
                            s.sendGridSelection(menu.selection, menu.step, mode)
                            close()
                        }
                    }
                }
                ArrangeMenu.Reference -> {
                    MenuItem("LOAD") { action(ArrangeAct.REF_LOAD) }
                    MenuItem("DELETE", red = true) { action(ArrangeAct.REF_DELETE) }
                }
            }
        }
    }
}
