package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.snapshotFlow
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import app.jambo.android.AppNative

/**
 * Главный экран (RootView.swift ветки swift JAMbo, сверху вниз): верхняя панель, центр (левая колонка | сетка ручек |
 * правая колонка | измеритель), строка дорожек, секвенсор, клавиатура. Меню звука — поверх левой колонки.
 */
@Composable
fun MainScreen(onMenu: () -> Unit) {
    val s = remember { ScreenState() }
    val dark = when (s.theme) { 1 -> false; 2 -> true; else -> isSystemInDarkTheme() }
    val th = if (dark) Theme.dark else Theme.light
    // Телефон (узкая сторона меньше 600 dp) — подписи крупнее.
    val phone = androidx.compose.ui.platform.LocalConfiguration.current.smallestScreenWidthDp < 600
    val textScale = if (phone) 1.2f else 1f
    val iconScale = if (phone) 1.6f else 1f
    canvasTextScale = textScale
    LaunchedEffect(Unit) {
        while (true) withFrameNanos { s.frame(it / 1e9) }
    }
    // Режим toggle FX и MOD — в настройках устройства, общий для проектов.
    val ctx = LocalContext.current
    LaunchedEffect(Unit) {
        val prefs = ctx.getSharedPreferences("jambo", 0)
        s.act(AppNative.Act.SET_LATCHED, if (prefs.getBoolean("latchFx", false)) 1 else 0,
            if (prefs.getBoolean("latchMod", false)) 1 else 0)
        snapshotFlow { Pair(s.side[2] != 0, s.side[3] != 0) }.collect { (fx, mod) ->
            prefs.edit().putBoolean("latchFx", fx).putBoolean("latchMod", mod).apply()
        }
    }
    // Меню клавиатуры (1 / 2 OCTAVES, KEY VELOCITY) и OVERDUB — в настройках устройства, общие для проектов.
    LaunchedEffect(Unit) {
        val prefs = ctx.getSharedPreferences("jambo", 0)
        s.act(AppNative.Act.SET_PIANO_SETTINGS, prefs.getInt("pianoOctaves", 1), if (prefs.getBoolean("velocity", false)) 1 else 0)
        s.act(AppNative.Act.SET_RECORD_SETTINGS, if (prefs.getBoolean("quantize", true)) 1 else 0,
            if (prefs.getBoolean("overdub", false)) 1 else 0)
        snapshotFlow { Triple(s.piano[2], s.piano[3] != 0, s.piano[4] != 0) }.collect { (oct, vel, dub) ->
            if (oct > 0) prefs.edit().putInt("pianoOctaves", oct).putBoolean("velocity", vel).putBoolean("overdub", dub).apply()
        }
    }
    // SETTINGS (QUANTIZE, MORPH, LEVEL, тема) — на устройстве, общие для проектов.
    LaunchedEffect(Unit) {
        val prefs = ctx.getSharedPreferences("jambo", 0)
        s.theme = prefs.getInt("theme", 0)
        s.act(AppNative.Act.SET_MORPH_SETTINGS, prefs.getInt("morphReturn", 0), if (prefs.getBoolean("morphHold", false)) 1 else 0)
        s.act(AppNative.Act.SET_LEVEL_RELATIVE, if (prefs.getBoolean("levelRelative", false)) 1 else 0)
        snapshotFlow { Pair(s.settings.toList(), s.theme) }.collect { (st, theme) ->
            if (st.size >= 5) prefs.edit().putBoolean("quantize", st[0] != 0).putInt("morphReturn", st[2])
                .putBoolean("morphHold", st[3] != 0).putBoolean("levelRelative", st[4] != 0).putInt("theme", theme).apply()
        }
    }
    // Фон темы — и под вырезом камеры; раскладка — внутри безопасной зоны.
    BoxWithConstraints(Modifier.fillMaxSize().background(th.bg).windowInsetsPadding(WindowInsets.displayCutout)) {
        // Раскладка — в точках iPad (Layout.swift): высота экрана — всегда designHeight точек, ширина — не меньше
        // designWidth (на широком экране лишнее растягивается). Плотность — пикселей на точку.
        val px = with(LocalDensity.current) { Pair(maxWidth.toPx(), maxHeight.toPx()) }
        val scale = minOf(px.first / Layout.designWidth, px.second / Layout.designHeight)
        CompositionLocalProvider(LocalTheme provides th, LocalDensity provides Density(scale, 1f), LocalTextScale provides textScale, LocalIconScale provides iconScale) {
            Screen(s, th, onMenu)
        }
    }
}

@Composable
private fun Screen(s: ScreenState, th: Theme, onMenu: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val width = maxWidth.value - 2 * Layout.side
        CompositionLocalProvider(LocalDims provides Dims.of(width, maxOf(1, s.piano.getOrElse(10) { 8 }))) { Body(s, th, onMenu, width) }
    }
}

@Composable
private fun Body(s: ScreenState, th: Theme, onMenu: () -> Unit, width: Float) {
    // Место левой колонки (для меню звука поверх неё), в координатах Body.
    var body by remember { mutableStateOf<LayoutCoordinates?>(null) }
    var left by remember { mutableStateOf(Rect.Zero) }
    var pagesRect by remember { mutableStateOf(Rect.Zero) }
    var gridRect by remember { mutableStateOf(Rect.Zero) }
    var renaming by remember { mutableStateOf(-1) }
    Box(Modifier.fillMaxSize().padding(horizontal = Layout.side.dp).onGloballyPositioned { body = it; s.body = it }) {
        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            TopBar(s) { s.mainMenu = !s.mainMenu }
            if (s.grid[0] != 0) PatternGrid(s, Modifier.weight(1f).fillMaxWidth(), width)
            else if (s.masterWin[0] >= 0) Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                MasterWindow(s, Modifier.weight(1f).fillMaxHeight())
                MeterColumn(s, Modifier.fillMaxHeight())
            }
            else if (s.masterWin.getOrElse(7) { 0 } != 0) Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                MixerWindow(s, Modifier.weight(1f).fillMaxHeight())
                MeterColumn(s, Modifier.fillMaxHeight())
            }
            else if (s.lib[0] != 0) Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                LibraryView(s, Modifier.weight(1f).fillMaxHeight())
                MeterColumn(s, Modifier.fillMaxHeight())
            }
            else if (s.eq[0] != 0) Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                EqView(s, Modifier.weight(1f).fillMaxHeight())
                MeterColumn(s, Modifier.fillMaxHeight())
            }
            else Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                LeftColumn(s, Modifier.width(column()).fillMaxHeight().onGloballyPositioned { c ->
                    body?.let { b -> if (b.isAttached) left = b.localBoundingBoxOf(c) }
                })
                KnobGrid(s, Modifier.weight(1f).fillMaxHeight().onGloballyPositioned { c ->
                    body?.let { b -> if (b.isAttached) gridRect = b.localBoundingBoxOf(c) }
                })
                RightColumn(s, Modifier.width(rightColumn()).fillMaxHeight()) { c ->
                    body?.let { b -> if (b.isAttached) pagesRect = b.localBoundingBoxOf(c) }
                }
                MeterColumn(s, Modifier.fillMaxHeight())
            }
            Box(Modifier.onGloballyPositioned { c ->
                body?.let { b -> if (b.isAttached) s.tracksRect = b.localBoundingBoxOf(c) }
            }) { TrackRow(s, Layout.step(width)) }
            SequencerRow(s)
            KeyboardPanel(s, width)
        }
        if (s.sound[2] != 0) SoundMenuOverlay(s, left)
        if (s.pageMenu >= 0) PageMenuOverlay(s, pagesRect)
        if (s.eq[0] != 0) EqMenus(s, s.eqUi)
        if (s.chordMenu) ChordMenuOverlay(s)
        if (s.pianoMenu) PianoMenuOverlay(s, Layout.step(width))
        s.choice?.let { ChoiceOverlay(s, it) }
        if (s.mixInfo.getOrElse(0) { -1 } >= 0) TrackMenuOverlay(s, gridRect) { renaming = it }
        if (renaming >= 0) InputDialog("RENAME", s.trackNames.getOrElse(renaming) { "" }, 12, { renaming = -1 }) { text ->
            AppNative.mixAct(10, renaming, 0, text)
            renaming = -1
            s.refresh()
        }
        if (s.mainMenu) MainMenuOverlay(s, onMenu)
    }
}

/** Меню страницы — под её миниатюрой, по ширине колонки миниатюр с обводкой; тап мимо — закрыть. */
@Composable
private fun PageMenuOverlay(s: ScreenState, r: Rect) {
    val d = LocalDensity.current.density
    val close = { s.pageMenu = -1 }
    Box(Modifier.fillMaxSize().pointerTap(close))
    val top = r.top / d
    val y = maxOf(0f, minOf(top + s.pageMenuY, r.bottom / d - PAGE_MENU_HEIGHT))
    PageMenu(s, s.pageMenu, Modifier.offset((r.left / d - 4).dp, y.dp).width((r.width / d + 8).dp)
        .pointerInput(Unit) { detectTapGestures { } }, close)
}

/** Меню звука — на месте левой колонки; тап мимо — закрыть. */
@Composable
private fun SoundMenuOverlay(s: ScreenState, r: Rect) {
    val d = LocalDensity.current.density
    Box(Modifier.fillMaxSize().pointerTap { s.act(AppNative.Act.CLOSE_SOUND_MENU) })
    SoundMenu(s, Modifier.offset((r.left / d).dp, (r.top / d).dp).size((r.width / d).dp, (r.height / d).dp)
        .pointerInput(Unit) { detectTapGestures { } })
}
