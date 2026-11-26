package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp

/** Меню бутерброда (MainMenu в RootView.swift): разделы PROJECT (PROJECTS), APP (SETTINGS), CONTROLLERS (MIDI, SYNC). Меню и подменю одного размера: от левого края бутерброда до
 *  правого края SCALE, от верхней панели до строки дорожек; что не помещается — прокрутка. Тап мимо — закрыть. */
@Composable
fun MainMenuOverlay(s: ScreenState, actions: ControllerActions) {
    val d = LocalDensity.current.density
    var page by remember { mutableStateOf(0) }
    val close = { s.mainMenu = false }
    val top = Layout.topHeight + Layout.gap
    val height = maxOf(200f, s.tracksRect.top / d - top - Layout.gap)
    val width = column().value + Layout.gap + mid(Layout.topButton).value
    Box(Modifier.fillMaxSize().pointerTap(close))
    Box(Modifier.offset(0.dp, top.dp).width(width.dp).height(height.dp).menuBox()
        .pointerInput(Unit) { detectTapGestures { } }.padding(12.dp)) {
        when (page) {
            1 -> ProjectsPage(s, back = { page = 0 }, close = close)
            2 -> SettingsPage(s, back = { page = 0 })
            3 -> MidiMenuPage(s.midi, actions, back = { page = 0 }, close = close)
            4 -> SyncMenuPage(s.midi, actions, back = { page = 0 })
            else -> Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(top = 4.dp, end = 6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                MenuHead("PROJECT")
                MenuItem("PROJECTS") {
                    s.act(app.jambo.android.AppNative.Act.REFRESH_PROJECTS)
                    page = 1
                }
                Divider()
                MenuHead("APP")
                MenuItem("SETTINGS") { page = 2 }
                Divider()
                MenuHead("CONTROLLERS")
                MenuItem("MIDI") { page = 3 }
                MenuItem("SYNC") { page = 4 }
            }
        }
    }
}

@Composable
fun Divider() {
    Box(Modifier.padding(vertical = 8.dp).fillMaxWidth().height(1.dp).background(LocalTheme.current.line))
}
