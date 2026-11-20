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
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.windowInsetsPadding

/**
 * Главный экран (RootView.swift ветки swift JAMbo, сверху вниз): верхняя панель, центр (левая колонка | сетка ручек |
 * правая колонка | измеритель), строка дорожек, секвенсор, клавиатура. Колонки и измеритель — пустые панели до своих вех.
 */
@Composable
fun MainScreen(onMenu: () -> Unit) {
    val th = if (isSystemInDarkTheme()) Theme.dark else Theme.light
    val s = remember { ScreenState() }
    LaunchedEffect(Unit) {
        while (true) withFrameNanos { s.frame(it / 1e9) }
    }
    // Фон темы — и под вырезом камеры; раскладка — внутри безопасной зоны.
    BoxWithConstraints(Modifier.fillMaxSize().background(th.bg).windowInsetsPadding(WindowInsets.displayCutout)) {
        // Раскладка — в точках iPad (Layout.swift): высота экрана — всегда designHeight точек, ширина — не меньше
        // designWidth (на широком экране лишнее растягивается). Плотность — пикселей на точку.
        val px = with(LocalDensity.current) { Pair(maxWidth.toPx(), maxHeight.toPx()) }
        val scale = minOf(px.first / Layout.designWidth, px.second / Layout.designHeight)
        CompositionLocalProvider(LocalTheme provides th, LocalDensity provides Density(scale, 1f)) {
            Screen(s, th, onMenu)
        }
    }
}

@Composable
private fun Screen(s: ScreenState, th: Theme, onMenu: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize().padding(horizontal = Layout.side.dp)) {
        val width = maxWidth.value
        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            TopBar(s, onMenu)
            Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                Box(Modifier.width(Layout.leftColumn.dp).fillMaxHeight().tile(th.panel))
                KnobGrid(s, Modifier.weight(1f).fillMaxHeight())
                Box(Modifier.width(Layout.leftColumn.dp).fillMaxHeight().tile(th.panel))
                Box(Modifier.width(Layout.meterWidth.dp).fillMaxHeight().tile(th.panel, 4f))
            }
            TrackRow(s, Layout.step(width))
            SequencerRow(s)
            KeyboardPanel(s, width)
        }
    }
}
