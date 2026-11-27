package app.jambo.android.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog

/** Заголовок раздела меню (menu-head): 12, жирный, разрядка 0,1em. */
@Composable
fun MenuHead(title: String) {
    Text(title, color = LocalTheme.current.text, fontSize = 12.tsp(), fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp)
}

/** Пояснение в меню: 12, цвет dim. */
@Composable
fun MenuNote(text: String) {
    Text(text, color = LocalTheme.current.dim, fontSize = 12.tsp())
}

/** Стрелка назад и заголовок вложенного меню. */
@Composable
fun MenuBackHeader(title: String, back: () -> Unit) {
    val th = LocalTheme.current
    Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Box(Modifier.width(40.dp).fillMaxHeight().pointerTap(back), contentAlignment = Alignment.Center) {
            Text("‹", color = th.text, fontSize = 26.isp())
        }
        Text(title, color = th.text, fontSize = 14.tsp(), fontWeight = FontWeight.Bold, maxLines = 1,
            overflow = TextOverflow.Ellipsis)
    }
}

/** Кнопки выбора в ряд (MenuChoiceButtons): выбранная — жёлтая. */
@Composable
fun MenuChoices(items: List<Pair<String, Boolean>>, enabled: List<Boolean>? = null, pick: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        items.forEachIndexed { i, (title, cur) ->
            MenuItem(title, Modifier.weight(1f), enabled = enabled?.getOrElse(i) { true } ?: true, current = cur) { pick(i) }
        }
    }
}

/** Строка списка (SwipeRow.swift): тап — действие; свайп влево открывает справа карандаш (EDIT) и корзину; строка
 *  сужается, имя остаётся видно; тап по открытой — закрыть. Открыта одна строка (open/setOpen — у списка). */
@Composable
fun SwipeRow(
    title: String, subtitle: String?, meta: String?, tint: Color?, current: Boolean, open: Boolean,
    setOpen: (Boolean) -> Unit, tap: () -> Unit, rename: (() -> Unit)?, delete: () -> Unit,
) {
    val th = LocalTheme.current
    val button = 52f
    val reveal = if (rename != null) 2 * button + 4 else button
    var drag by remember { mutableFloatStateOf(0f) }
    var dragging by remember { mutableStateOf(false) }
    val target = if (dragging) drag else if (open) -reveal else 0f
    val off by animateFloatAsState(target, label = "swipe")
    val shown = if (dragging) drag else off
    Box(Modifier.fillMaxWidth().height(48.dp)) {
        if (shown < -1) {
            Row(Modifier.align(Alignment.CenterEnd).fillMaxHeight(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                if (rename != null) {
                    Box(Modifier.width(button.dp).fillMaxHeight().tile(th.panel2, 8f).pointerTap { setOpen(false); rename() },
                        contentAlignment = Alignment.Center) { Text("EDIT", color = th.text, fontSize = 11.tsp(), fontWeight = FontWeight.Bold) }
                }
                Box(Modifier.width(button.dp).fillMaxHeight().tile(th.rec, 8f).pointerTap { setOpen(false); delete() },
                    contentAlignment = Alignment.Center) { Text("🗑", color = Color.White, fontSize = 16.tsp()) }
            }
        }
        val bg = if (current) th.accent else th.panel
        val fg = if (current) th.onAccent else tint ?: th.text
        Row(
            Modifier.fillMaxHeight().fillMaxWidth().padding(end = (-shown).coerceAtLeast(0f).dp).tile(bg, 8f)
                .pointerInput(open) {
                    detectHorizontalDragGestures(
                        onDragStart = { dragging = true; drag = if (open) -reveal else 0f },
                        onDragEnd = { dragging = false; setOpen(drag < -reveal / 2) },
                        onDragCancel = { dragging = false },
                    ) { _, dx -> drag = (drag + dx / density).coerceIn(-reveal, 0f) }
                }
                .pointerTap { if (open) setOpen(false) else tap() }
                .padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(title, color = fg, fontSize = 13.tsp(), fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (subtitle != null) Text(subtitle, color = if (current) th.onAccent else th.dim, fontSize = 10.tsp(), maxLines = 1)
            }
            if (meta != null && shown > -1) Text(meta, color = fg, fontSize = 11.tsp(), maxLines = 1)
        }
    }
}

/** Окно ввода имени: поле (обведено белым), CANCEL и OK; ввод — limit знаков (подставленное длиннее — можно). */
@Composable
fun InputDialog(
    title: String, initial: String, limit: Int, cancel: () -> Unit,
    extra: Pair<String, () -> Unit>? = null, enter: (String) -> Unit,
) {
    val th = LocalTheme.current
    var value by remember { mutableStateOf(TextFieldValue(initial, TextRange(0, initial.length))) }
    val focus = remember { FocusRequester() }
    Dialog(onDismissRequest = cancel) {
        Column(Modifier.width(320.dp).background(th.bg, RoundedCornerShape(12.dp)).border(1.dp, th.line, RoundedCornerShape(12.dp))
            .padding(14.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, color = th.text, fontSize = 14.tsp(), fontWeight = FontWeight.Bold)
            BasicTextField(
                value, { v -> if (v.text.length <= limit || v.text.length <= value.text.length) value = v },
                singleLine = true, textStyle = TextStyle(color = th.text, fontSize = 15.tsp()), cursorBrush = SolidColor(th.text),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { enter(value.text) }),
                modifier = Modifier.fillMaxWidth().focusRequester(focus).border(1.dp, th.text, RoundedCornerShape(8.dp))
                    .padding(10.dp),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MenuItem("CANCEL", Modifier.weight(1f)) { cancel() }
                if (extra != null) MenuItem(extra.first, Modifier.weight(1f)) { extra.second() }
                MenuItem("OK", Modifier.weight(1f)) { enter(value.text) }
            }
        }
    }
    LaunchedEffect(Unit) { focus.requestFocus() }
}

/** Окно с текстом и кнопками (вопрос о перезаписи, LOAD / MIX). */
@Composable
fun ChoiceDialog(text: String, note: String?, buttons: List<Triple<String, Boolean, () -> Unit>>, cancel: () -> Unit) {
    val th = LocalTheme.current
    Dialog(onDismissRequest = cancel) {
        Column(Modifier.width(320.dp).background(th.bg, RoundedCornerShape(12.dp)).border(1.dp, th.line, RoundedCornerShape(12.dp))
            .padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(text, color = th.text, fontSize = 14.tsp())
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for ((t, on, a) in buttons) MenuItem(t, Modifier.weight(1f), enabled = on) { a() }
            }
            if (note != null) MenuNote(note)
        }
    }
}
