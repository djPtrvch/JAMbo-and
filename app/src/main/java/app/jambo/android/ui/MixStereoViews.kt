package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import app.jambo.android.AppNative

/**
 * Стерео по полосам при MIX (BandStereoView ветки swift JAMbo): ячейки LOW, MID, HIGH WIDTH — подпись в две строки и
 * ширина в %, под ними — полоса корреляции −1…+1 от середины: жёлтая — норма, оранжевая — у LOW ниже 0,7, красная —
 * ниже 0 (противофаза). Тап — сброс.
 */
@Composable
fun BandStereoView(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val dim = TextStyle(color = th.dim, fontSize = 9.tsp())
    val bold = TextStyle(fontSize = 12.tsp(), fontWeight = FontWeight.SemiBold)
    val v = s.mixStereo
    Box(modifier.tile(th.panel).pointerTap { AppNative.masterAct(4, 0) }) {
        Canvas(Modifier.fillMaxSize()) {
            if (v.size < 9) return@Canvas
            val cell = size.width / 3
            val barY = maxOf(48 * density, (size.height + 40 * density) / 2 - 6 * density)
            for ((b, name) in listOf("LOW", "MID", "HIGH").withIndex()) {
                val x = cell * (b + 0.5f)
                val on = v[3 * b] != 0f
                val c = v[3 * b + 1]
                val col = if (c < 0) th.rec else if (b == 0 && c < 0.7f) th.gr else th.accent
                label(text, dim, listOf(name, "WIDTH"), x, 4 * density)
                val value = text.measure(if (on) "%.0f%%".format(v[3 * b + 2]) else "—",
                    bold.copy(color = if (on && col != th.accent) col else th.text))
                drawText(value, topLeft = Offset(x - value.size.width / 2, 27 * density))
                corrBar(th, text, dim, x - cell / 2 + 10 * density, barY, cell - 20 * density, if (on) c else null, col)
            }
        }
    }
}

/** Подпись в несколько строк по центру x (9 точек, --dim). */
private fun DrawScope.label(text: TextMeasurer, dim: TextStyle, lines: List<String>, x: Float, y: Float) {
    for ((k, l) in lines.withIndex()) {
        val t = text.measure(l, dim)
        drawText(t, topLeft = Offset(x - t.size.width / 2, y + k * 11 * density))
    }
}

/** Полоса корреляции: от середины вправо — к +1, влево — к −1; подписи −1 и +1 снизу. */
private fun DrawScope.corrBar(th: Theme, text: TextMeasurer, dim: TextStyle, x0: Float, y: Float, w: Float, c: Float?, col: Color) {
    val h = 4.5f * density
    drawRoundRect(th.meterTrack, Offset(x0, y), Size(w, h), CornerRadius(2 * density))
    val mid = x0 + w / 2
    if (c != null) {
        val x = mid + c * w / 2
        drawRect(col, Offset(minOf(mid, x), y), Size(maxOf(2f, kotlin.math.abs(x - mid)), h))
    }
    drawRect(th.dim, Offset(mid - density / 2, y - 2 * density), Size(density, h + 4 * density))
    text.measure("−1", dim).let { drawText(it, topLeft = Offset(x0, y + h + 3 * density)) }
    text.measure("+1", dim).let { drawText(it, topLeft = Offset(x0 + w - it.size.width, y + h + 3 * density)) }
}

/**
 * История громкости мастера при MIX (LoudHistoryView): SHORT LOUDNESS (short-term сейчас), LOUDNESS RANGE (LRA),
 * PEAK TO LOUDNESS (PLR); ниже — short-term за последние 60 с игры (−6…−24 LUFS), пунктир — интегральная. Тап —
 * с нуля (и LUFS измерителя).
 */
@Composable
fun LoudHistoryView(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val dim = TextStyle(color = th.dim, fontSize = 9.tsp())
    val bold = TextStyle(color = th.text, fontSize = 12.tsp(), fontWeight = FontWeight.SemiBold)
    val v = s.mixStereo
    Box(modifier.tile(th.panel).pointerTap { AppNative.masterAct(5, 0); s.refresh() }) {
        Canvas(Modifier.fillMaxSize()) {
            if (v.size < 13) return@Canvas
            fun fmt(x: Float) = if (x > -998) "%.1f".format(x) else "—"
            val cell = size.width / 3
            val cells = listOf(listOf("SHORT", "LOUDNESS") to fmt(v[9]), listOf("LOUDNESS", "RANGE") to
                (if (v[12] >= 0) "%.1f".format(v[12]) else "—"), listOf("PEAK TO", "LOUDNESS") to fmt(v[11]))
            for ((k, c) in cells.withIndex()) {
                val x = cell * (k + 0.5f)
                label(text, dim, c.first, x, 4 * density)
                val t = text.measure(c.second, bold)
                drawText(t, topLeft = Offset(x - t.size.width / 2, 27 * density))
            }
            val row = 44 * density
            val left = 6 * density
            val top = row + 4 * density
            val w = size.width - 12 * density
            val h = maxOf(1f, size.height - row - 10 * density)
            fun yOf(x: Float) = top + ((-6f - x) / 18f).coerceIn(0f, 1f) * h
            for (db in listOf(-12, -18, -24)) drawLine(th.line, Offset(left, yOf(db.toFloat())), Offset(left + w, yOf(db.toFloat())), density)
            for (db in listOf(-12, -24)) text.measure("$db", dim).let {
                drawText(it, topLeft = Offset(left + density, yOf(db.toFloat()) - density - it.size.height))
            }
            if (v[10] > -998) drawLine(th.text.copy(alpha = 0.6f), Offset(left, yOf(v[10])), Offset(left + w, yOf(v[10])), density,
                pathEffect = PathEffect.dashPathEffect(floatArrayOf(3 * density, 3 * density)))
            val n = v.size - 13
            if (n < 2) return@Canvas
            val dx = w / (n - 1)
            val p = Path()
            var pen = false
            for (k in 0 until n) {
                val x = v[13 + k]
                if (x > -998) {
                    if (pen) p.lineTo(left + k * dx, yOf(x)) else p.moveTo(left + k * dx, yOf(x))
                    pen = true
                } else pen = false
            }
            drawPath(p, th.accent, style = Stroke(1.5f * density, join = StrokeJoin.Round))
        }
    }
}
