package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.rememberTextMeasurer

/** Кнопка вида полос MIX (MixModeButton): тап — этот вид; выбранная — белая. */
@Composable
fun MixModeButton(s: ScreenState, title: String, mode: Int, modifier: Modifier) {
    SideButton(title, modifier, lit = s.mixMode == mode) { s.mixMode = mode }
}

/**
 * Обзор MIXER в окне сэмпла (MixOverview из SampleWindow.swift ветки swift JAMbo): по полосе на дорожку, 16 мест на
 * всю ширину, под полосами — номера дорожек; заглушённая — серая. LEVEL — уровень (наибольший из L и R), −60…0 dBFS,
 * выше 0 dBFS — красный, пик-холд — линия цвета текста; PAN — баланс сигнала (R − L) / (R + L): от средней линии вверх —
 * вправо, вниз — влево. Слой LOUD у LEVEL, FREQ и MASK — с анализом спектров (позже).
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
        val w = 9 * density
        val n = v.getOrElse(0) { 0f }.toInt()
        val cy = Math.round(h / 2).toFloat()
        val r = CornerRadius(3 * density)
        if (mode == 1) {
            drawRect(th.line, Offset(0f, cy), Size(size.width, density))
            text.measure("R", dim).let { drawText(it, topLeft = Offset(size.width - it.size.width, 0f)) }
            text.measure("L", dim).let { drawText(it, topLeft = Offset(size.width - it.size.width, h - it.size.height)) }
        }
        for (t in 0 until n) {
            val o = 1 + 8 * t
            if (o + 8 > v.size) break
            val x = cell * (t + 0.5f) - w / 2
            val audible = v[o + 5] != 0f
            val col = if (audible) th.accent else th.muteGrey
            if (mode == 1) {
                val l = v[o + 6]
                val rr = v[o + 7]
                if (maxOf(l, rr) >= 0.001f) {
                    val b = (rr - l) / (rr + l)
                    val bh = maxOf(2 * density, kotlin.math.abs(b) * h / 2)
                    drawRoundRect(col, Offset(x, if (b >= 0) cy + density - bh else cy), Size(w, bh), r)
                }
            } else {
                val f = maxOf(v[o], v[o + 1])
                val over = audible && v[o + 4] != 0f
                if (f > 0) drawRoundRect(if (over) th.rec else col, Offset(x, h - f * h), Size(w, f * h), r)
                val hf = maxOf(v[o + 2], v[o + 3])
                if (hf > 0.01f) drawRect(th.text, Offset(x, maxOf(0f, h - hf * h - density)), Size(w, 2 * density))
            }
            val num = text.measure("${t + 1}", dim)
            drawText(num, topLeft = Offset(cell * (t + 0.5f) - num.size.width / 2, size.height - num.size.height))
        }
    }
}
