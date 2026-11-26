package app.jambo.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import app.jambo.android.PageAct

/**
 * Правая колонка (SideColumns.swift ветки swift JAMbo): у дорожки — стрелки страниц ◀ ▶ ▲ ▼, миниатюры страниц, FX и
 * MOD, SHIFT и MORPH. MIX и MST (скоп мастера) — позже.
 * onPages — место колонки миниатюр (меню страницы встаёт под миниатюрой).
 */
@Composable
fun RightColumn(s: ScreenState, modifier: Modifier, onPages: (LayoutCoordinates) -> Unit) {
    val th = LocalTheme.current
    val channel = s.tracks[2]
    BoxWithConstraints(modifier) {
        if (channel == 1) {
            // Скоп мастера того же размера, что при MST; на месте осциллоскопа и кнопок — позже стерео по полосам и
            // история громкости.
            MasterScope(s, Modifier.fillMaxSize(), osc = false)
            return@BoxWithConstraints
        }
        if (channel == 2) {
            Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                MasterScope(s, Modifier.weight(1f).fillMaxWidth())
                ButtonPair { ScopeButton(s, 1, Modifier.weight(1f)); ScopeButton(s, 2, Modifier.weight(1f)) }
                ButtonPair {
                    HoldButton("SHIFT", s.side[4] != 0, Modifier.weight(1f).midiLearnMark(s, 37)) { on -> if (s.midi.controls.learning) { if (on) s.learnTarget(37) }
                    else s.act(Act.SET_SHIFT, if (on) 1 else 0) }
                    ScopeButton(s, 3, Modifier.weight(1f))
                }
            }
            return@BoxWithConstraints
        }
        val bar = Side.bar(maxHeight.value)
        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            Column(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(Side.barGap.dp)) {
                PageArrows(s, Modifier.height(bar.dp))
                PagesColumn(s, Modifier.weight(1f).fillMaxWidth().onGloballyPositioned(onPages))
            }
            // FX слева, MOD справа.
            ButtonPair { PageButton(s, "FX", 0, Modifier.weight(1f)); PageButton(s, "MOD", 1, Modifier.weight(1f)) }
            ButtonPair {
                HoldButton("SHIFT", s.side[4] != 0, Modifier.weight(1f).midiLearnMark(s, 37)) { on -> if (s.midi.controls.learning) { if (on) s.learnTarget(37) }
                    else s.act(Act.SET_SHIFT, if (on) 1 else 0) }
                MorphButton(s, Modifier.weight(1f))
            }
        }
    }
}

/** FX и MOD: удержание или (SHIFT + нажатие) режим toggle — тогда внутри вторая чёрная линия контура. */
@Composable
private fun PageButton(s: ScreenState, title: String, page: Int, modifier: Modifier) {
    val latch = s.side[2 + page] != 0
    val line = if (latch) androidx.compose.ui.graphics.Color.Black else androidx.compose.ui.graphics.Color.Transparent
    HoldButton(title, s.side[page] != 0, modifier.midiLearnMark(s, 38 + page).drawWithContent {
        drawContent()
        val i = 3.dp.toPx()
        drawRoundRect(line, androidx.compose.ui.geometry.Offset(i, i),
            androidx.compose.ui.geometry.Size(size.width - 2 * i, size.height - 2 * i),
            androidx.compose.ui.geometry.CornerRadius((Layout.radius - 3).dp.toPx()),
            style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
    }) { on ->
        if (s.midi.controls.learning) { if (on) s.learnTarget(38 + page) }
        else s.act(if (on) Act.PAGE_PRESS else Act.PAGE_RELEASE, page)
    }
}

/**
 * MORPH: тап включает и выключает режим (жёлтая — включён; при касании сразу видно, каким станет режим); удержание +
 * UNDO — сбросить изменения режима; SHIFT + MORPH — на все дорожки, пока держат. Чёрная линия — режим тапом (MODE TAP).
 */
@Composable
private fun MorphButton(s: ScreenState, modifier: Modifier) {
    val line = if (s.side[9] != 0) androidx.compose.ui.graphics.Color.Black else androidx.compose.ui.graphics.Color.Transparent
    val cb by androidx.compose.runtime.rememberUpdatedState<(Boolean) -> Unit>({ on ->
        s.act(if (on) Act.MORPH_PRESS else Act.MORPH_RELEASE)
    })
    SideButton("MORPH", modifier.drawWithContent {
        drawContent()
        val i = 3.dp.toPx()
        drawRoundRect(line, androidx.compose.ui.geometry.Offset(i, i),
            androidx.compose.ui.geometry.Size(size.width - 2 * i, size.height - 2 * i),
            androidx.compose.ui.geometry.CornerRadius((Layout.radius - 3).dp.toPx()),
            style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
    }.pointerInput(Unit) {
        awaitEachGesture {
            awaitFirstDown(requireUnconsumed = false).consume()
            cb(true)
            waitForUpOrCancellation()
            cb(false)
        }
    }, accent = s.side[8] != 0)
}

/** Стрелки страниц ◀ ▶ ▲ ▼ над миниатюрами — для открытой страницы (в белой обводке — все играющие): ◀ ▶ — шаги
 *  по кругу, ▲ ▼ — ноты на октаву (с SHIFT — на ноту гаммы — с кнопкой SHIFT). */
@Composable
private fun PageArrows(s: ScreenState, modifier: Modifier) {
    Row(modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        SideButton("◀", Modifier.weight(1f), icon = true) { s.act(Act.PAGE_ARROW, PageAct.LEFT) }
        SideButton("▶", Modifier.weight(1f), icon = true) { s.act(Act.PAGE_ARROW, PageAct.RIGHT) }
        SideButton("▲", Modifier.weight(1f), icon = true) { s.act(Act.PAGE_ARROW, PageAct.UP) }
        SideButton("▼", Modifier.weight(1f), icon = true) { s.act(Act.PAGE_ARROW, PageAct.DOWN) }
    }
}

/** Высота меню страницы: 5 строк по 36, SPEED — подпись 11 и две строки по 30, зазоры по 6, поля 8. */
const val PAGE_MENU_HEIGHT = 5 * 36f + 11 + 2 * 30 + 7 * 6 + 16

/** Меню страницы: duplicate, copy, paste, clear, delete, SPEED. Действуют на все играющие страницы, если страница в
 *  белой обводке, иначе только на неё; delete — только на неё. */
@Composable
fun PageMenu(s: ScreenState, page: Int, modifier: Modifier, close: () -> Unit) {
    val th = LocalTheme.current
    val pg = s.pages
    val shape = RoundedCornerShape(9.dp)
    val run = { a: Int -> s.act(Act.PAGE_ACTION, a, page); close() }
    Column(modifier.shadow(12.dp, shape).background(th.bg, shape).border(1.dp, th.line, shape).padding(8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)) {
        PageItem("DUPLICATE", pg.getOrElse(7 + page * 17) { 0 } != 0) { run(PageAct.DUPLICATE) }
        PageItem("COPY", true) { run(PageAct.COPY) }
        PageItem("PASTE", pg[6] != 0) { run(PageAct.PASTE) }
        PageItem("CLEAR", true) { run(PageAct.CLEAR) }
        PageItem("DELETE", pg[0] > 1, red = true) { run(PageAct.DELETE) }
        Text("SPEED", color = th.dim, fontSize = 9.tsp())
        for (row in 0 until 2) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (col in 0 until 3) {
                    val k = row * 3 + col
                    val rate = s.clipRates.getOrElse(k) { 0 }
                    InkButton(s.clipRateNames.getOrElse(k) { "" }, rate == pg[5], Modifier.weight(1f)) {
                        s.act(Act.SET_CLIP_RATE, rate)
                    }
                }
            }
        }
    }
}

@Composable
private fun PageItem(title: String, enabled: Boolean, red: Boolean = false, action: () -> Unit) {
    val th = LocalTheme.current
    val color = if (!enabled) th.off else if (red) th.rec else th.text
    val tap = if (enabled) Modifier.pointerTap(action) else Modifier
    Box(Modifier.fillMaxWidth().height(36.dp).tile(th.panel, 8f).then(tap), contentAlignment = androidx.compose.ui.Alignment.Center) {
        Text(title, color = color, fontSize = 13.tsp(), fontWeight = FontWeight.Bold)
    }
}

/** Кнопка выбора (InkButton): выбранная — жёлтая. */
@Composable
fun InkButton(title: String, active: Boolean, modifier: Modifier, height: Float = 30f, action: () -> Unit) {
    val th = LocalTheme.current
    Box(modifier.height(height.dp).tile(if (active) th.accent else th.panel).pointerTap(action),
        contentAlignment = androidx.compose.ui.Alignment.Center) {
        Text(title, color = if (active) th.onAccent else th.text, fontSize = 13.tsp(), fontWeight = FontWeight.Bold, maxLines = 1)
    }
}
