package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import kotlin.math.hypot
import kotlin.math.ln
import kotlin.math.log2
import kotlin.math.pow
import kotlin.math.roundToInt

/** Координаты поля EQ (EqGeometry в EqPlot.swift): 10 Гц…20 кГц логарифмически по ширине, ±21 dB на всю высоту. */
private class EqGeom(val w: Float, val h: Float) {
    val octaves = log2(20000.0 / 10.0)
    fun x(f: Double) = (w * log2(f / 10.0) / octaves).toFloat()
    fun freq(x: Float) = 10.0 * 2.0.pow(x / maxOf(1f, w) * octaves)
    fun y(g: Double) = (h / 2 - g / 21.0 * h / 2).toFloat()
    fun gain(y: Float) = ((h / 2 - y) / maxOf(1f, h / 2) * 21.0)
}

/** Палец на поле: начало, где сейчас, полоса (−1 — пустое место) и её значения в начале, сдвинулся ли. */
private class EqTouch(val start: Offset, var at: Offset, val band: Int, val f0: Double, val g0: Double, val q0: Double) {
    var moved = false
}

/** Поле EQ (EqPlot.swift): сетка, спектры до и после EQ, суммарная кривая, точки полос; мультитач. Двойной тап по
 *  пустому месту — новая bell-полоса; точку тянут (у cut и notch по вертикали — Q); двойной тап по точке — выкл./вкл.
 *  полосы; меню полосы — кнопкой TYPE. Над пальцем, который держит точку, — частота (Гц и нота), усиление, Q. */
@Composable
fun EqPlot(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val touches = remember { mutableStateMapOf<PointerId, EqTouch>() }
    val last = remember { arrayOfNulls<Any>(1) }
    BoxWithConstraints(modifier.tile(th.panel)) {
        val wDp = maxWidth.value
        s.eqCols = (wDp / 3).toInt() + 1
        Canvas(Modifier.fillMaxSize().pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) {
                    val ev = awaitPointerEvent()
                    val g = EqGeom(size.width.toFloat(), size.height.toFloat())
                    for (c in ev.changes) {
                        if (c.pressed && !c.previousPressed) {
                            val k = hitBand(s, g, c.position, touches.values.map { it.band })
                            val b = s.eqBands
                            if (k >= 0) {
                                s.act(Act.SELECT_EQ_BAND, k)
                                s.act(Act.BEGIN_GESTURE)
                            }
                            touches[c.id] = EqTouch(c.position, c.position, k,
                                if (k >= 0) b[5 * k].toDouble() else 0.0, if (k >= 0) b[5 * k + 1].toDouble() else 0.0,
                                if (k >= 0) b[5 * k + 2].toDouble() else 1.0)
                        } else if (c.pressed) {
                            val t = touches[c.id] ?: continue
                            t.at = c.position
                            if (!t.moved && hypot(c.position.x - t.start.x, c.position.y - t.start.y) > 8 * density) t.moved = true
                            touches[c.id] = t
                            if (t.band >= 0 && t.moved) drag(s, g, t, c.position)
                        } else {
                            val t = touches.remove(c.id) ?: continue
                            if (t.band >= 0) s.act(Act.END_GESTURE)
                            if (!t.moved) tapped(s, g, t, last, density)
                        }
                        c.consume()
                    }
                }
            }
        }) {
            val g = EqGeom(size.width, size.height)
            drawGrid(this, g, th, text)
            drawSpectrum(this, s, th)
            drawCurves(this, s, g, th)
            drawPoints(this, s, g, th, text)
            for (t in touches.values) if (t.band >= 0) drawTip(this, s, g, t, th, text)
        }
    }
}

/** Ближайшая точка в зоне касания 24 dp (выбранная — в приоритете); одну точку держит один палец. */
private fun androidx.compose.ui.input.pointer.AwaitPointerEventScope.hitBand(
    s: ScreenState, g: EqGeom, p: Offset, held: List<Int>,
): Int {
    var best = -1
    var bestD = 24 * density
    for (k in 0 until 10) {
        if (s.eq.getOrElse(5 + 6 * k) { 0 } == 0 || k in held) continue
        val c = point(s, g, k, false)
        val bias = if (k == s.eq[3]) 6 * density else 0f
        val d = hypot(c.x - p.x, c.y - p.y) - bias
        if (d < bestD) {
            bestD = d
            best = k
        }
    }
    return best
}

/** Точка полосы: у cut и notch — на 0 dB; dyn — со сдвигом динамики. */
private fun point(s: ScreenState, g: EqGeom, k: Int, dyn: Boolean): Offset {
    val b = s.eqBands
    val type = s.eq[5 + 6 * k + 2]
    var gain = if (eqHasGain(type)) b[5 * k + 1].toDouble() else 0.0
    if (dyn) gain += s.eqCurve.getOrElse(1 + 3 * s.eqCols + k) { 0f }
    return Offset(g.x(b[5 * k].toDouble()), g.y(gain))
}

private fun drag(s: ScreenState, g: EqGeom, t: EqTouch, p: Offset) {
    val type = s.eq[5 + 6 * t.band + 2]
    val dx = ((p.x - t.start.x) / maxOf(1f, g.w)).toDouble()
    val f = t.f0 * 2.0.pow(dx * g.octaves)
    if (eqHasGain(type)) {
        AppNative.eqSetBand(t.band, f, t.g0 + g.gain(p.y) - g.gain(t.start.y), Double.NaN)
    } else {
        // Cut и notch: по вертикали — Q (вверх — больше), весь ход 0,1…18 — на высоту поля, по логарифму.
        val dy = ((t.start.y - p.y) / maxOf(1f, g.h)).toDouble()
        val q = (t.q0 * (18.0 / 0.1).pow(dy)).coerceIn(0.1, 18.0)
        AppNative.eqSetBand(t.band, f, Double.NaN, q)
    }
    s.refresh()
}

/** Двойной тап: по точке — выкл./вкл. полосы, по пустому месту — новая bell-полоса. */
private fun tapped(s: ScreenState, g: EqGeom, t: EqTouch, last: Array<Any?>, density: Float) {
    val now = System.currentTimeMillis()
    @Suppress("UNCHECKED_CAST")
    val l = last[0] as Triple<Long, Offset, Int>?
    if (l != null && now - l.first < 320 && hypot(l.second.x - t.start.x, l.second.y - t.start.y) < 30 * density &&
        l.third == t.band) {
        if (t.band >= 0) s.act(Act.TOGGLE_EQ_BAND, t.band)
        else {
            AppNative.eqAddBand(g.freq(t.start.x), g.gain(t.start.y))
            s.refresh()
        }
        last[0] = null
    } else {
        last[0] = Triple(now, t.start, t.band)
    }
}

private fun drawGrid(d: DrawScope, g: EqGeom, th: Theme, text: androidx.compose.ui.text.TextMeasurer) {
    val grid = th.text.copy(alpha = 0.07f)
    val strong = th.text.copy(alpha = 0.19f)
    var dec = 10.0
    while (dec <= 10000) {
        for (m in 1..9) {
            val f = dec * m
            if (f < 10 || f > 20000) continue
            val x = g.x(f)
            d.drawLine(if (m == 1) strong else grid, Offset(x, 0f), Offset(x, g.h), 1f)
        }
        dec *= 10
    }
    for (db in -18..18 step 6) {
        val y = g.y(db.toDouble())
        d.drawLine(if (db == 0) strong else grid, Offset(0f, y), Offset(g.w, y), 1f)
    }
    val style = TextStyle(color = th.dim, fontSize = 11.sp)
    for ((f, l) in listOf(100.0 to "100 Hz", 1000.0 to "1 kHz", 10000.0 to "10 kHz")) {
        val r = text.measure(l, style)
        d.drawText(r, topLeft = Offset(g.x(f) + 4 * d.density, g.h - r.size.height - 4 * d.density))
    }
    for (db in listOf(12, 0, -12)) {
        val r = text.measure(if (db > 0) "+$db dB" else "$db dB", style)
        d.drawText(r, topLeft = Offset(6 * d.density, g.y(db.toDouble()) - 8 * d.density - r.size.height / 2))
    }
}

/** Спектры: до EQ — серая линия, после EQ — серая полупрозрачная заливка. */
private fun drawSpectrum(d: DrawScope, s: ScreenState, th: Theme) {
    val sp = s.eqSpec
    val n = sp.size / 2
    if (n < 2) return
    val h = d.size.height
    val step = d.size.width / (n - 1)
    fun path(off: Int, filled: Boolean): Path {
        val p = Path()
        if (filled) {
            p.moveTo(0f, h)
            p.lineTo(0f, sp[off] * h)
        } else p.moveTo(0f, sp[off] * h)
        for (i in 1 until n - 1) {
            val x = i * step
            p.quadraticTo(x, sp[off + i] * h, x + step / 2, (sp[off + i] + sp[off + i + 1]) / 2 * h)
        }
        p.lineTo((n - 1) * step, sp[off + n - 1] * h)
        if (filled) {
            p.lineTo((n - 1) * step, h)
            p.close()
        }
        return p
    }
    d.drawPath(path(n, true), th.dim.copy(alpha = 0.22f))
    d.drawPath(path(0, false), th.dim.copy(alpha = 0.6f), style = Stroke(1.2f * d.density))
}

/** Кривые: белая — стерео-полосы (и когда полос MID/SIDE нет); середина — красная, бока — оранжевая. */
private fun drawCurves(d: DrawScope, s: ScreenState, g: EqGeom, th: Theme) {
    val c = s.eqCurve
    val n = s.eqCols
    if (c.size < 1 + 3 * n || n < 2) return
    val bits = c[0].toInt()
    val a = if (s.eq[2] != 0) 1f else 0.35f
    val lim = 21.0 * 1.2
    fun curve(off: Int, color: Color) {
        val p = Path()
        for (i in 0 until n) {
            val x = g.w * i / (n - 1)
            val y = g.y(c[off + i].toDouble().coerceIn(-lim, lim))
            if (i == 0) p.moveTo(x, y) else p.lineTo(x, y)
        }
        d.drawPath(p, color.copy(alpha = a), style = Stroke(2 * d.density))
    }
    if (bits and 1 != 0 || bits and 6 == 0) curve(1, th.text)
    if (bits and 2 != 0) curve(1 + n, th.rec)
    if (bits and 4 != 0) curve(1 + 2 * n, th.gr)
}

/** Точки: цвет режима M/S с номерами, выбранная крупнее и жёлтая, выключенные серые; динамическая — со сдвигом, на
 *  месте статического усиления — полое кольцо. */
private fun drawPoints(d: DrawScope, s: ScreenState, g: EqGeom, th: Theme, text: androidx.compose.ui.text.TextMeasurer) {
    for (k in 0 until 10) {
        val base = 5 + 6 * k
        if (s.eq.getOrElse(base) { 0 } == 0) continue
        val on = s.eq[base + 1] != 0
        val ms = s.eq[base + 4]
        val dynOn = on && eqHasGain(s.eq[base + 2]) && s.eq[base + 5] >= 0
        val sel = k == s.eq[3]
        val r = (if (sel) 13f else 10f) * d.density
        val msColor = if (ms == 1) th.rec else if (ms == 2) th.gr else th.text
        val c = point(s, g, k, dynOn)
        if (dynOn) d.drawCircle(msColor.copy(alpha = 0.7f), r - d.density, point(s, g, k, false), style = Stroke(1.5f * d.density))
        d.drawCircle(if (!on) th.muteGrey else if (sel) th.accent else msColor, r, c)
        if (sel && on && ms > 0) d.drawCircle(msColor, r + 2.5f * d.density, c, style = Stroke(3 * d.density))
        val t = text.measure("${k + 1}", TextStyle(color = if (sel) th.onAccent else th.bg, fontSize = if (sel) 13.sp else 11.sp,
            fontWeight = FontWeight.SemiBold))
        d.drawText(t, topLeft = Offset(c.x - t.size.width / 2, c.y - t.size.height / 2))
    }
}

/** Подсказка над пальцем: частота · нота, усиление · Q. */
private fun drawTip(d: DrawScope, s: ScreenState, g: EqGeom, t: EqTouch, th: Theme, text: androidx.compose.ui.text.TextMeasurer) {
    val b = s.eqBands
    val k = t.band
    val f = b[5 * k].toDouble()
    val hz = if (f < 1000) (if (f < 100) "%.1f Hz" else "%.0f Hz").format(java.util.Locale.US, f)
        else (if (f < 10000) "%.2f kHz" else "%.1f kHz").format(java.util.Locale.US, f / 1000)
    val note = noteName((69 + 12 * ln(f / 440) / ln(2.0)).roundToInt().coerceAtLeast(0))
    val gain = if (eqHasGain(s.eq[5 + 6 * k + 2])) "%+.1f dB".format(java.util.Locale.US, b[5 * k + 1]) else "—"
    val r = text.measure("$hz · $note\n$gain · Q ${"%.2f".format(java.util.Locale.US, b[5 * k + 2])}",
        TextStyle(color = th.text, fontSize = 13.sp, textAlign = TextAlign.Center))
    val pad = 9 * d.density
    val w = r.size.width + 2 * pad
    val h = r.size.height + 10 * d.density
    val cx = t.at.x.coerceIn(w / 2, maxOf(w / 2, g.w - w / 2))
    val cy = maxOf(h / 2, t.at.y - 68 * d.density)
    d.drawRoundRect(th.panel2, Offset(cx - w / 2, cy - h / 2), Size(w, h), CornerRadius(8 * d.density))
    d.drawText(r, topLeft = Offset(cx - r.size.width / 2, cy - r.size.height / 2))
}

private val NOTES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
private fun noteName(n: Int) = NOTES[n % 12] + (n / 12 - 1)
