package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import app.jambo.android.StepBits

/** Размеры ряда шагов по числу показанных страниц (SeqLook в SequencerRow.swift). */
class SeqLook(n: Int) {
    val pages = maxOf(1, n)
    val gap: Float
    val pageGap: Float
    val inset: Float
    val dot: Float
    val dotPad: Float
    val showLabel = pages <= 2

    init {
        when (pages) {
            1 -> { gap = Layout.stepGap; pageGap = 0f; inset = 5f; dot = 6f; dotPad = 10f }
            2 -> { gap = 2f; pageGap = 6f; inset = 3f; dot = 5f; dotPad = 6f }
            3, 4 -> { gap = 1f; pageGap = 5f; inset = 1f; dot = 4f; dotPad = 3f }
            else -> { gap = 1f; pageGap = 3f; inset = 1f; dot = 3f; dotPad = 2f }
        }
    }

    /** Шаг в ряду под x (0 … 16·pages − 1); x и width — в dp. */
    fun column(x: Float, width: Float): Int {
        val pageW = (width - pageGap * (pages - 1)) / pages
        val p = ((x / (pageW + pageGap)).toInt()).coerceIn(0, pages - 1)
        val x0 = x - p * (pageW + pageGap)
        val cell = (pageW + gap) / 16
        val c = ((x0 / cell).toInt()).coerceIn(0, 15)
        return p * 16 + c
    }
}

/** Нажатый шаг. */
private class StepPress(val col: Int, val index: Int, val t0: Long, val secondary: Boolean, var used: Boolean) {
    var dragged = false
}

/** Касания ряда шагов (жесты SequencerRow.swift). */
private class SeqTouches(val s: ScreenState) {
    val presses = HashMap<PointerId, StepPress>()
    val lastTap = HashMap<Int, Long>()

    fun down(id: PointerId, col: Int, first: Int, now: Long) {
        val index = first * 16 + col
        val secondary = presses.values.any { it.col == 0 } && col != 0
        var lengthSet = false
        val t = lastTap[col]
        if (secondary && t != null && now - t < 350) {
            // Двойной тап: первый тап скопировал первый шаг — копия отменяется, шаг становится последним.
            s.act(Act.RESTORE_COPY, index)
            s.act(Act.SET_PATTERN_LENGTH, index)
            presses.values.filter { it.col == 0 }.forEach { it.used = true }
            lastTap.remove(col)
            lengthSet = true
        }
        presses[id] = StepPress(col, index, now, secondary, lengthSet)
        s.act(Act.HOLD_STEP, index, 1)
        s.act(Act.BEGIN_GESTURE)
    }

    fun move(id: PointerId, col: Int) {
        val p = presses[id] ?: return
        if (p.secondary) return
        if (col == p.col && !p.dragged) return
        if (col < p.col) return
        p.dragged = true
        p.used = true
        s.act(Act.SET_NOTE_LENGTH, p.index, col - p.col + 1)
    }

    fun finish(id: PointerId, now: Long) {
        val p = presses.remove(id) ?: return
        val used = p.used || s.act(Act.STEP_USED, p.index) != 0.0
        if (presses.values.none { it.index == p.index }) s.act(Act.HOLD_STEP, p.index, 0)
        try {
            val quick = now - p.t0 < 350
            if (quick && !p.dragged) lastTap[p.col] = now
            if (!quick || p.dragged || used) return
            // Зажат другой шаг (раньше нажатый) — копия его в этот.
            val src = presses.values.filter { it.index != p.index && it.t0 < p.t0 }.minByOrNull { it.t0 }
            if (src != null) {
                s.act(Act.COPY_STEP, src.index, p.index)
                src.used = true
                return
            }
            if (!p.secondary) s.act(Act.TOGGLE_STEP, p.index)
        } finally {
            s.act(Act.END_GESTURE)
        }
    }
}

/** Шаги открытой страницы — или всех играющих (16 × N в один ряд). */
@Composable
fun SequencerRow(s: ScreenState) {
    val data = s.steps
    val first = data[0]
    val last = data[1]
    val look = SeqLook(last - first + 1)
    val touches = remember(s) { SeqTouches(s) }
    val pos = s.playPos
    Row(
        Modifier.fillMaxWidth().height(Layout.seqHeight.dp).pointerInput(look.pages, first) {
            awaitPointerEventScope {
                while (true) {
                    val ev = awaitPointerEvent()
                    val w = size.width / density
                    for (c in ev.changes) {
                        val col = look.column(c.position.x / density, w)
                        val now = c.uptimeMillis
                        when {
                            c.changedToDown() -> touches.down(c.id, col, first, now)
                            c.changedToUp() -> touches.finish(c.id, now)
                            c.pressed && c.positionChanged() -> touches.move(c.id, col)
                        }
                        c.consume()
                    }
                }
            }
        },
        horizontalArrangement = Arrangement.spacedBy(look.pageGap.dp),
    ) {
        for (page in first..last) {
            Row(Modifier.weight(1f).fillMaxHeight(), horizontalArrangement = Arrangement.spacedBy(look.gap.dp)) {
                for (col in 0 until 16) {
                    val k = (page - first) * 16 + col
                    StepCell(
                        bits = data.getOrElse(2 + k) { 0 }, label = s.stepLabels.getOrElse(k) { "" }, col = col,
                        playing = pos == page * 16 + col, look = look, modifier = Modifier.weight(1f),
                    )
                }
            }
        }
    }
}

/** Шаг: триг (белый), хвост (полупрозрачный), нота или POLY, играющий и свой звук — жёлтый контур, зажатый — белый,
 * ручные p-lock — жёлтая точка (4-го столбца — оранжевая), за длиной паттерна — бледный, последний — жёлтая черта. */
@Composable
private fun StepCell(bits: Int, label: String, col: Int, playing: Boolean, look: SeqLook, modifier: Modifier) {
    val th = LocalTheme.current
    val on = bits and StepBits.ON != 0
    val tail = bits and StepBits.TAIL != 0
    val outer = if (look.pages > 2) 3f else 6f
    val inner = if (look.pages > 2) 2f else 4f
    val outline = when {
        playing || bits and StepBits.OWN_SOUND != 0 -> th.accent
        bits and StepBits.HELD != 0 -> th.text
        else -> Color.Transparent
    }
    val fill = if (on) th.text else if (tail) th.text.copy(alpha = 0.4f) else Color.Transparent
    Box(modifier.fillMaxHeight().alpha(if (bits and StepBits.OUTSIDE != 0) 0.3f else 1f)) {
        Box(
            Modifier.fillMaxSize()
                .background(if (col % 4 == 0) th.panel2 else th.panel, RoundedCornerShape(outer.dp))
                .border(1.dp, outline, RoundedCornerShape(outer.dp))
                .padding(look.inset.dp),
        ) {
            Box(Modifier.fillMaxSize().background(fill, RoundedCornerShape(inner.dp)), contentAlignment = Alignment.Center) {
                if (look.showLabel && label.isNotEmpty()) {
                    Text(label, color = th.bg, fontSize = if (look.pages > 1) 10.sp else 11.sp, fontWeight = FontWeight.Bold,
                        maxLines = 1)
                }
            }
        }
        val manual = bits and StepBits.MANUAL_LOCK != 0
        val col4 = bits and StepBits.COL4_LOCK != 0
        if (manual || col4) {
            Row(Modifier.padding(look.dotPad.dp), horizontalArrangement = Arrangement.spacedBy((look.dot * 0.5f).dp)) {
                if (manual) Box(Modifier.size(look.dot.dp).background(th.accent, CircleShape))
                if (col4) Box(Modifier.size(look.dot.dp).background(th.gr, CircleShape))
            }
        }
        if (bits and StepBits.LAST != 0) {
            Box(
                Modifier.align(Alignment.CenterEnd).offset(x = (look.gap / 2 + 1).dp).width(2.dp).fillMaxHeight()
                    .padding(vertical = 6.dp).background(th.accent),
            )
        }
    }
}
