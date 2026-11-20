package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import app.jambo.android.KNOB_STRIDE
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

private const val TRAVEL = 240f
private const val DEAD = 4f
private const val SPACING = 2f

/** Поворот ручки пальцем. */
private class KnobTurn(val slot: Int, val startY: Float, var lastY: Float, var acc: Double) {
    var moved = false
    var consumed = false
    /** Назначение источника (1–4): накопитель — степень −1…+1. */
    var lfo = 0
    /** SHIFT + касание: при отпускании — тап (Filter — тип, Dly Time и Speed LFO — по темпу / свободно…). */
    var shiftTap = false
}

private class KnobTouches(val s: ScreenState) {
    val turns = HashMap<PointerId, KnobTurn>()
    val lastTap = HashMap<Int, Long>()

    fun down(id: PointerId, slot: Int, y: Float, now: Long) {
        val turn = KnobTurn(slot, y, y, s.act(Act.KNOB_START, slot))
        // SHIFT для ручек: пока держат SHIFT + MORPH, ручки крутятся как без неё.
        val shift = s.side[7] != 0
        val mix = s.tracks[2] == 1
        val t = lastTap[slot]
        if (t != null && now - t < 350) {
            lastTap.remove(slot)
            // SHIFT + двойной тап снимает все p-lock: режим, переключённый первым тапом, — обратно.
            if (shift) s.act(Act.KNOB_SHIFT_TAP, slot, 1)
            // С зажатым LFO/ENV — снять назначение этого источника.
            s.act(Act.KNOB_DOUBLE_TAP, slot)
            turn.consumed = true
        } else {
            lastTap[slot] = now
            if (shift && !mix) turn.shiftTap = true
        }
        // С зажатой SHIFT ручки не крутятся.
        if (shift) turn.consumed = true
        // Удерживаемый LFO/ENV (без зажатого шага) — степень назначения −1…+1.
        val src = s.act(Act.ASSIGN_SOURCE, slot).toInt()
        if (src > 0) {
            turn.lfo = src
            turn.acc = s.act(Act.ASSIGN_START, slot, src)
        }
        s.act(Act.KNOB_FINGER, slot, 1)
        turns[id] = turn
        s.act(Act.BEGIN_GESTURE)
    }

    fun move(id: PointerId, y: Float) {
        val t = turns[id] ?: return
        if (t.consumed) return
        if (!t.moved) {
            if (abs(y - t.startY) < DEAD) return
            t.moved = true
            t.shiftTap = false
            t.lastY = y
            return
        }
        val d = ((t.lastY - y) / TRAVEL).toDouble()
        t.lastY = y
        if (t.lfo > 0) {
            t.acc = (t.acc + d).coerceIn(-1.0, 1.0)
            s.act(Act.SET_ASSIGN, t.slot, t.lfo, t.acc)
            return
        }
        t.acc = (t.acc + d).coerceIn(0.0, 1.0)
        s.act(Act.SET_KNOB, t.slot, 0, t.acc)
    }

    fun up(id: PointerId) {
        val turn = turns.remove(id) ?: return
        if (turn.shiftTap) s.act(Act.KNOB_SHIFT_TAP, turn.slot, 0)
        if (turns.values.none { it.slot == turn.slot }) s.act(Act.KNOB_FINGER, turn.slot, 0)
        s.act(Act.END_GESTURE)
    }
}

/** Сетка ручек 4×4: вертикальное движение пальца, мультитач; двойной тап — снять p-lock или к значению по умолчанию. */
@Composable
fun KnobGrid(s: ScreenState, modifier: Modifier) {
    val touches = remember(s) { KnobTouches(s) }
    Column(
        modifier.pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) {
                    val ev = awaitPointerEvent()
                    for (c in ev.changes) {
                        val y = c.position.y / density
                        when {
                            c.changedToDown() -> {
                                val col = (c.position.x / (size.width / 4f)).toInt().coerceIn(0, 3)
                                val row = (c.position.y / (size.height / 4f)).toInt().coerceIn(0, 3)
                                touches.down(c.id, row * 4 + col, y, c.uptimeMillis)
                            }
                            c.changedToUp() -> touches.up(c.id)
                            c.pressed -> touches.move(c.id, y)
                        }
                        c.consume()
                    }
                }
            }
        },
        verticalArrangement = Arrangement.spacedBy(SPACING.dp),
    ) {
        for (row in 0 until 4) {
            Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(SPACING.dp)) {
                for (col in 0 until 4) {
                    val k = row * 4 + col
                    val o = k * KNOB_STRIDE
                    val v = s.knobs
                    val lfo = v[o + 6].toInt()
                    val mod = if (lfo > 0) s.modNorms[k].takeIf { it >= 0 } else null
                    val modText = if (mod != null) s.modTexts[k] else ""
                    KnobCell(
                        active = v[o] > 0, norm = v[o + 1], base = v[o + 2], locked = v[o + 3] > 0,
                        bipolar = v[o + 4] > 0, col4 = v[o + 5] > 0,
                        mod = ModRing(lfo, v[o + 7], v[o + 8], v[o + 9], v[o + 10] > 0, mod),
                        label = s.knobTexts.getOrElse(k * 2) { "" },
                        value = modText.ifEmpty { s.knobTexts.getOrElse(k * 2 + 1) { "" } },
                        modifier = Modifier.weight(1f).fillMaxHeight(),
                    )
                }
            }
        }
    }
}

/** Ручка: кольцо, дуга значения (от центра у двухполярных), p-lock — внутренняя дуга жёлтым (4-я строка — оранжевым). */
/** Назначение LFO/ENV на ручке: источник (0 — нет), степень, ход источника, держат его, положение с модуляцией. */
private class ModRing(val lfo: Int, val amount: Float, val lo: Float, val hi: Float, val held: Boolean, val norm: Float?)

@Composable
private fun KnobCell(
    active: Boolean, norm: Float, base: Float, locked: Boolean, bipolar: Boolean, col4: Boolean, mod: ModRing,
    label: String, value: String, modifier: Modifier,
) {
    val th = LocalTheme.current
    val lockColor = if (col4) th.gr else th.accent
    Column(modifier.padding(vertical = 2.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
            Canvas(Modifier.fillMaxSize()) {
                val side = min(size.width, size.height)
                val k = side / 100f
                val c = Offset(size.width / 2, size.height / 2)
                arc(c, 36 * k, 0f, 1f, if (active) th.off else th.panel, 6 * k)
                drawCircle(if (active) th.panel2 else th.panel, 24 * k, c)
                if (!active) return@Canvas
                val origin = if (bipolar) 0.5f else 0f
                arc(c, 36 * k, origin, base, th.text, 6 * k)
                if (locked) arc(c, 28 * k, origin, norm, lockColor, 5 * k)
                if (mod.lfo > 0) drawMod(c, k, norm, mod, th.text)
                val a = angle(mod.norm ?: norm)
                drawLine(if (locked) lockColor else th.text, polar(c, 21 * k, a), polar(c, 28 * k, a), 3 * k,
                    StrokeCap.Round)
            }
            Text(value, color = if (locked) lockColor else th.text, fontSize = 11.tsp(), textAlign = TextAlign.Center)
        }
        Text(label, color = th.dim, fontSize = 11.tsp(), maxLines = 1)
    }
}

/**
 * Кольцо модуляции (r 45) от значения до текущего (LFO1 ярче всех, LFO2 и LFO3 — по убыванию; ENV — пунктир); зона
 * воздействия — пока держат этот источник: по ходу источника, со знаком степени.
 */
private fun DrawScope.drawMod(c: Offset, k: Float, n: Float, m: ModRing, ink: Color) {
    if (m.held) {
        val op = when (m.lfo) { 1 -> 0.18f; 2 -> 0.13f; 3 -> 0.09f; else -> 0.12f }
        val e0 = n + m.amount * m.lo
        val e1 = n + m.amount * m.hi
        val lo = maxOf(0f, min(e0, e1))
        val hi = min(1f, maxOf(e0, e1))
        arc(c, 45 * k, lo, hi, ink.copy(alpha = op), 8 * k)
    }
    val cur = m.norm ?: return
    val ops = floatArrayOf(0.95f, 0.7f, 0.45f, 0.85f)
    val op = ops[(m.lfo - 1).coerceIn(0, 3)]
    if (m.lfo == 4) {
        arc(c, 45 * k, n, cur, ink.copy(alpha = op), 4 * k, StrokeCap.Butt,
            PathEffect.dashPathEffect(floatArrayOf(3 * k, 3 * k)))
    } else {
        arc(c, 45 * k, n, cur, ink.copy(alpha = op), 4 * k)
    }
}

/** Угол положения n (0…1): ход 270° от −135° (вверх — 0). */
private fun angle(n: Float): Float = ((-135 + 270 * n - 90) * Math.PI / 180).toFloat()

private fun polar(c: Offset, r: Float, a: Float) = Offset(c.x + r * cos(a), c.y + r * sin(a))

private fun DrawScope.arc(
    c: Offset, r: Float, n0: Float, n1: Float, color: Color, width: Float, cap: StrokeCap = StrokeCap.Round,
    effect: PathEffect? = null,
) {
    if (abs(n1 - n0) < 0.002f) return
    val a0 = Math.toDegrees(angle(min(n0, n1)).toDouble()).toFloat()
    val a1 = Math.toDegrees(angle(maxOf(n0, n1)).toDouble()).toFloat()
    drawArc(color, a0, a1 - a0, false, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r),
        style = Stroke(width, cap = cap, pathEffect = effect))
}
