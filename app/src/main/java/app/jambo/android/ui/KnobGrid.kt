package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.zIndex
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
import app.jambo.android.AppNative
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
    /** MIX: время касания и поднятая ручка (удержание 0,5 с без движения — перестановка дорожек). */
    var downAt = 0L
    var lifted = false
    /** Палец при касании в координатах сетки, px (перенос поднятой ручки). */
    var startPx = 0f
    var startPy = 0f
}

private class KnobTouches(val s: ScreenState) {

    val turns = HashMap<PointerId, KnobTurn>()
    val lastTap = HashMap<Int, Long>()

    /** MIX: кнопка M, S, ● под пальцем (0, 1, 2) или −1; local — в точках ячейки. */
    fun mixButton(local: androidx.compose.ui.geometry.Offset, cell: androidx.compose.ui.geometry.Size): Int {
        for ((k, r) in mixButtons(cell).withIndex())
            if (local.x >= r.left - 4 && local.x <= r.right + 4 && local.y >= r.top - 2 && local.y <= r.bottom + 2) return k
        return -1
    }

    fun down(id: PointerId, slot: Int, y: Float, now: Long, px: Float = 0f, py: Float = 0f) {
        val turn = KnobTurn(slot, y, y, s.act(Act.KNOB_START, slot))
        turn.downAt = now
        turn.startPx = px
        turn.startPy = py

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

    /** Перенос поднятой ручки: px — палец в координатах сетки, cell — шаг ячейки (px). */
    fun drag(id: PointerId, px: Float, py: Float, cw: Float, ch: Float) {
        val t = turns[id] ?: return
        if (!t.lifted) return
        s.liftDx = px - t.startPx
        s.liftDy = py - t.startPy
        val col = (px / cw).toInt().coerceIn(0, 3)
        val row = (py / ch).toInt().coerceIn(0, 3)
        s.liftTarget = minOf(s.tracks[0] - 1, row * 4 + col)
    }

    fun move(id: PointerId, y: Float, now: Long) {
        val t = turns[id] ?: return
        if (t.consumed || t.lifted) return
        if (!t.moved) {
            if (abs(y - t.startY) < DEAD) return
            // MIX: держали без движения 0,5 с — ручка поднята (чуть крупнее, над остальными), палец переносит дорожку.
            if (s.tracks[2] == 1 && t.slot < s.tracks[0] && now - t.downAt >= 500) {
                t.lifted = true
                s.liftSlot = t.slot
                s.liftTarget = t.slot
                return
            }
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
        if (turn.lifted) {
            val target = s.liftTarget
            if (target >= 0 && target != turn.slot && target < s.tracks[0]) AppNative.mixAct(11, turn.slot, target, "")
            s.liftSlot = -1
            s.liftTarget = -1
            s.liftDx = 0f
            s.liftDy = 0f
        }
        if (turn.shiftTap) s.act(Act.KNOB_SHIFT_TAP, turn.slot, 0)
        if (turns.values.none { it.slot == turn.slot }) s.act(Act.KNOB_FINGER, turn.slot, 0)
        s.act(Act.END_GESTURE)
    }
}

/** Сетка ручек 4×4: вертикальное движение пальца, мультитач; двойной тап — снять p-lock или к значению по умолчанию. */
@Composable
fun KnobGrid(s: ScreenState, modifier: Modifier) {
    val touches = remember(s) { KnobTouches(s) }
    var gridSize by remember { mutableStateOf(androidx.compose.ui.unit.IntSize.Zero) }
    val sp = SPACING * androidx.compose.ui.platform.LocalDensity.current.density
    Column(
        modifier.onSizeChanged { gridSize = it }.pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) {
                    val ev = awaitPointerEvent()
                    for (c in ev.changes) {
                        val y = c.position.y / density
                        val col = (c.position.x / (size.width / 4f)).toInt().coerceIn(0, 3)
                        val row = (c.position.y / (size.height / 4f)).toInt().coerceIn(0, 3)
                        when {
                            c.changedToDown() -> {
                                val slot = if (s.tracks[2] != 1 && s.arrangePreferences.knobColumns) col * 4 + row else row * 4 + col
                                val cw = (size.width - 3 * SPACING * density) / 4 / density
                                val ch = (size.height - 3 * SPACING * density) / 4 / density
                                val local = androidx.compose.ui.geometry.Offset(
                                    c.position.x / density - col * (cw + SPACING), c.position.y / density - row * (ch + SPACING))
                                val b = if (s.tracks[2] == 1 && slot < s.tracks[0])
                                    touches.mixButton(local, androidx.compose.ui.geometry.Size(cw, ch)) else -1
                                val target = if (b >= 0) mixButtonMidiTarget(b, slot) else s.midi.controls.knobTarget(slot)
                                if (s.learnTarget(target)) {
                                    c.consume()
                                    continue
                                }
                                if (b >= 0) {
                                    AppNative.mixAct(b + 1, slot, 0, "")
                                    s.refresh()
                                } else touches.down(c.id, slot, y, c.uptimeMillis, c.position.x, c.position.y)
                            }
                            c.changedToUp() -> touches.up(c.id)
                            c.pressed -> {
                                touches.move(c.id, y, c.uptimeMillis)
                                touches.drag(c.id, c.position.x, c.position.y,
                                    (size.width + SPACING * density) / 4f, (size.height + SPACING * density) / 4f)
                            }
                        }
                        c.consume()
                    }
                }
            }
        },
        verticalArrangement = Arrangement.spacedBy(SPACING.dp),
    ) {
        for (row in 0 until 4) {
            Row(Modifier.weight(1f).fillMaxWidth().zIndex(if (s.liftSlot >= 0 && s.liftSlot / 4 == row) 1f else 0f),
                horizontalArrangement = Arrangement.spacedBy(SPACING.dp)) {
                for (col in 0 until 4) {
                    val mixCell = s.tracks[2] == 1
                    val k = if (!mixCell && s.arrangePreferences.knobColumns) col * 4 + row else row * 4 + col
                    val o = k * KNOB_STRIDE
                    val v = s.knobs
                    val lfo = v[o + 6].toInt()
                    val mod = if (lfo > 0) s.modNorms[k].takeIf { it >= 0 } else null
                    val modText = if (mod != null) s.modTexts[k] else ""
                    // Перестановка в MIX: поднятая ручка идёт за пальцем (чуть крупнее, над остальными), дорожки между
                    // ней и местом под пальцем плавно сдвигаются на соседние места.
                    val from = s.liftSlot
                    val to = s.liftTarget
                    val dest = when {
                        from < 0 || to < 0 || k == from -> k
                        from < to && k in (from + 1)..to -> k - 1
                        from > to && k in to until from -> k + 1
                        else -> k
                    }
                    val cw = (gridSize.width + sp) / 4f
                    val ch = (gridSize.height + sp) / 4f
                    val sx by animateFloatAsState(if (mixCell) ((dest % 4) - col) * cw else 0f, label = "mixShiftX")
                    val sy by animateFloatAsState(if (mixCell) ((dest / 4) - row) * ch else 0f, label = "mixShiftY")
                    val lifted = k == from
                    val cellMod = Modifier.zIndex(if (lifted) 1f else 0f).graphicsLayer {
                        if (lifted) {
                            translationX = s.liftDx
                            translationY = s.liftDy
                            scaleX = 1.08f
                            scaleY = 1.08f
                            shadowElevation = 12 * density
                        } else {
                            translationX = sx
                            translationY = sy
                        }
                    }
                    KnobCell(
                        mix = mixCell,
                        active = v[o] > 0, norm = v[o + 1], base = v[o + 2], locked = v[o + 3] > 0,
                        bipolar = v[o + 4] > 0, col4 = v[o + 5] > 0,
                        mod = ModRing(lfo, v[o + 7], v[o + 8], v[o + 9], v[o + 10] > 0, mod),
                        label = s.knobTexts.getOrElse(k * 2) { "" },
                        value = modText.ifEmpty { s.knobTexts.getOrElse(k * 2 + 1) { "" } },
                        modifier = Modifier.weight(1f).fillMaxHeight().then(cellMod)
                            .midiLearnMark(s, s.midi.controls.knobTarget(k)),
                        overlay = { if (mixCell && k < s.tracks[0]) MixCellParts(s, k) },
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
    mix: Boolean = false, overlay: @Composable () -> Unit = {},
    active: Boolean, norm: Float, base: Float, locked: Boolean, bipolar: Boolean, col4: Boolean, mod: ModRing,
    label: String, value: String, modifier: Modifier,
) {
    val th = LocalTheme.current
    val lockColor = if (col4) th.gr else th.accent
    Box(modifier) {
    // MIX: слева метр (21), справа столбик M, S, ● (26).
    Column(Modifier.fillMaxSize().padding(vertical = 2.dp).padding(start = if (mix) (MIX_METER + 4).dp else 0.dp,
        end = if (mix) (MIX_BUTTON + 6).dp else 0.dp), horizontalAlignment = Alignment.CenterHorizontally) {
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
    overlay()
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
