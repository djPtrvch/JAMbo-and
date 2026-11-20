package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.rememberTextMeasurer
import app.jambo.android.AppNative

/** Чисел на дорожку в mixBars. */
private const val STRIDE = 73
private const val BANDS = 30

/** Кнопка вида полос MIX (MixModeButton): тап — этот вид; выбранная — белая. */
@Composable
fun MixModeButton(s: ScreenState, title: String, mode: Int, modifier: Modifier) {
    SideButton(title, modifier, lit = s.mixMode == mode) { AppNative.masterAct(3, mode); s.refresh() }
}

/**
 * Обзор MIXER в окне сэмпла (MixOverview из SampleWindow.swift ветки swift JAMbo): по полосе на дорожку, 16 мест,
 * под полосами — номера дорожек; заглушённая — серая. LEVEL — пик полупрозрачный (выше 0 dBFS — красный), снизу
 * сплошной слой LOUD, пик-холд — линия; PAN — баланс (R − L) / (R + L); FREQ — диапазон частот дорожки (20 Гц…20 кГц,
 * самая сильная — красная черта с градиентом); MASK — диапазон серый, жёлтым — конфликты, над ним номер мешающей.
 */
@Composable
fun MixOverview(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val dim = TextStyle(color = th.dim, fontSize = 9.tsp())
    val v = s.mixBars
    val mode = s.mixMode
    Canvas(modifier) {
        val numbersH = 14 * density
        val h = size.height - numbersH
        val cell = size.width / 16
        val n = v.getOrElse(0) { 0f }.toInt()
        grid(mode, h, th, text, dim)
        for (t in 0 until n) {
            val o = 1 + STRIDE * t
            if (o + STRIDE > v.size) break
            when (mode) {
                1 -> pan(v, o, t, cell, h, th)
                2 -> freq(v, o, t, cell, h, th)
                3 -> mask(v, o, t, cell, h, th, text, dim)
                else -> level(v, o, t, cell, h, th)
            }
            val num = text.measure("${t + 1}", dim)
            drawText(num, topLeft = Offset(cell * (t + 0.5f) - num.size.width / 2, size.height - num.size.height))
        }
    }
}

/** Сетка и подписи справа: PAN — R, L; FREQ и MASK — 100, 1k, 10k. */
private fun DrawScope.grid(mode: Int, h: Float, th: Theme, text: TextMeasurer, dim: TextStyle) {
    if (mode == 1) {
        drawRect(th.line, Offset(0f, Math.round(h / 2).toFloat()), Size(size.width, density))
        text.measure("R", dim).let { drawText(it, topLeft = Offset(size.width - it.size.width, 0f)) }
        text.measure("L", dim).let { drawText(it, topLeft = Offset(size.width - it.size.width, h - it.size.height)) }
    } else if (mode >= 2) {
        val ys = listOf(0.2330f, 0.5663f, 0.8997f)
        for ((k, name) in listOf("100", "1k", "10k").withIndex()) {
            val y = Math.round(h - ys[k] * h).toFloat()
            drawRect(th.line.copy(alpha = 0.6f), Offset(0f, y), Size(size.width, density))
            text.measure(name, dim).let { drawText(it, topLeft = Offset(size.width - it.size.width, y - density - it.size.height)) }
        }
    }
}

private fun DrawScope.level(v: FloatArray, o: Int, t: Int, cell: Float, h: Float, th: Theme) {
    val w = 9 * density
    val x = cell * (t + 0.5f) - w / 2
    val r = CornerRadius(3 * density)
    val audible = v[o + 5] != 0f
    val col = if (audible) th.accent else th.muteGrey
    val f = maxOf(v[o], v[o + 1])
    val lf = v[o + 8]
    val over = audible && v[o + 4] != 0f
    if (f > 0) drawRoundRect(if (over) th.rec else col.copy(alpha = 0.4f), Offset(x, h - f * h), Size(w, f * h), r)
    if (lf > 0) drawRoundRect(col, Offset(x, h - lf * h), Size(w, lf * h), r)
    val hf = maxOf(v[o + 2], v[o + 3])
    if (hf > 0.01f) drawRect(th.text, Offset(x, maxOf(0f, h - hf * h - density)), Size(w, 2 * density))
}

private fun DrawScope.pan(v: FloatArray, o: Int, t: Int, cell: Float, h: Float, th: Theme) {
    val w = 9 * density
    val x = cell * (t + 0.5f) - w / 2
    val col = if (v[o + 5] != 0f) th.accent else th.muteGrey
    val l = v[o + 6]
    val rr = v[o + 7]
    if (maxOf(l, rr) < 0.001f) return
    val cy = Math.round(h / 2).toFloat()
    val b = (rr - l) / (rr + l)
    val bh = maxOf(2 * density, kotlin.math.abs(b) * h / 2)
    drawRoundRect(col, Offset(x, if (b >= 0) cy + density - bh else cy), Size(w, bh), CornerRadius(3 * density))
}

private fun DrawScope.freq(v: FloatArray, o: Int, t: Int, cell: Float, h: Float, th: Theme) {
    val lo = v[o + 9]
    val hi = v[o + 10]
    val peak = v[o + 11]
    if (lo < 0) return
    val w = 9 * density
    val x = cell * (t + 0.5f) - w / 2
    val audible = v[o + 5] != 0f
    val rect = Rect(Offset(x, h - hi * h), Size(w, maxOf(3 * density, (hi - lo) * h)))
    val shape = Path().apply { addRoundRect(RoundRect(rect, CornerRadius(3 * density))) }
    drawPath(shape, if (audible) th.accent else th.muteGrey)
    if (!audible) return
    // Самая сильная частота — красная черта, от неё вверх и вниз — короткий градиент.
    val py = h - peak * h
    val g = 14 * density
    clipPath(shape) {
        drawRect(Brush.verticalGradient(listOf(th.rec.copy(alpha = 0f), th.rec, th.rec.copy(alpha = 0f)), py - g, py + g),
            Offset(x, py - g), Size(w, 2 * g))
        drawRect(th.rec, Offset(x, py - density), Size(w, 2 * density))
    }
}

private fun DrawScope.mask(v: FloatArray, o: Int, t: Int, cell: Float, h: Float, th: Theme, text: TextMeasurer, dim: TextStyle) {
    val lo = v[o + 9]
    val hi = v[o + 10]
    if (lo < 0) return
    val wide = maxOf(6 * density, minOf(10 * density, cell - 2 * density))
    val narrow = 6 * density
    val cx = cell * (t + 0.5f)
    val top = h - hi * h
    val height = maxOf(3 * density, (hi - lo) * h)
    if (v[o + 5] == 0f) {
        drawRoundRect(th.muteGrey, Offset(cx - wide / 2, top), Size(wide, height), CornerRadius(3 * density))
        return
    }
    val bh = h / BANDS
    val str = o + 13
    val bur = o + 13 + BANDS
    val first = Math.round(lo * BANDS)
    val last = minOf(BANDS, Math.round(hi * BANDS)) - 1
    var j = first
    while (j <= last) {
        val buried = v[bur + j] != 0f
        var k = j
        while (k + 1 <= last && (v[bur + k + 1] != 0f) == buried) k++
        val y = h - (k + 1) * bh
        val hh = (k - j + 1) * bh
        if (buried) {
            drawRoundRect(th.accent, Offset(cx - narrow / 2, y), Size(narrow, hh), CornerRadius(2 * density))
        } else {
            val run = Rect(Offset(cx - wide / 2, y), Size(wide, hh))
            val shape = Path().apply { addRoundRect(RoundRect(run, CornerRadius(3 * density))) }
            drawPath(shape, th.meterTrack)
            clipPath(shape) {
                for (q in j..k) {
                    val sv = v[str + q]
                    if (sv > 0.02f) drawRect(th.accent.copy(alpha = sv.coerceIn(0f, 1f)), Offset(run.left, h - (q + 1) * bh), Size(wide, bh))
                }
            }
        }
        j = k + 1
    }
    val who = v[o + 12].toInt()
    if (who < 0) return
    val label = text.measure("${who + 1}", dim)
    val ly = if (top >= 12 * density) top - 2 * density - label.size.height else top + height + 2 * density
    drawText(label, topLeft = Offset(cx - label.size.width / 2, ly))
}
