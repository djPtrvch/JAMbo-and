package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative

/** Модули мастера (MasterScope): SAT 0, COMP 1, CLIP 2, LIM 3 — имена и цвета (формы волны скопа, рамки кнопок). */
private val moduleNames = listOf("SAT", "COMP", "CLIP", "LIM")

private fun moduleColor(th: Theme, k: Int) = when (k) {
    0 -> th.text
    1 -> th.accent
    2 -> th.gr
    else -> th.rec
}

/**
 * Индикация мастера при MST в окне сэмпла (MasterIndicator.swift ветки swift JAMbo): блоки COMP | CLIP | LIM, в
 * каждом стержни IN | GR | OUT (9 точек, скругление 3): IN и OUT — снизу вверх, −48…0 dBFS, жёлтые, выше 0 — красные;
 * GR — сверху вниз, оранжевый (COMP — 18 dB, CLIP и LIM — 12 dB); риска THRESHOLD / CEILING — на IN; bypass — серые.
 * Тап по блоку — окно модуля.
 */
@Composable
fun MasterIndicator(s: ScreenState, modifier: Modifier) {
    val v = s.masterBars
    Row(modifier.padding(8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        for (k in 0 until 3) Column(Modifier.weight(1f).fillMaxHeight()
            .pointerTap { AppNative.masterWinAct(app.jambo.android.MasterWinAct.OPEN, k + 1, 0, ""); s.refresh() },
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(moduleNames[k + 1], color = LocalTheme.current.dim, fontSize = 10.tsp(), letterSpacing = 1.sp)
            DynBars(v, k, if (k == 0) 18f else 12f, Modifier.weight(1f).fillMaxWidth())
        }
    }
}

@Composable
fun DynBars(v: FloatArray, k: Int, grMax: Float, modifier: Modifier, scale: Float = 48f, alignTop: Boolean = false) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val labelStyle = TextStyle(color = th.dim, fontSize = 9.tsp())
    Canvas(modifier) {
        val o = 7 * k
        if (v.size < o + 7) return@Canvas
        val inDb = v[o]; val outDb = v[o + 1]; val gr = v[o + 2]
        val inRed = v[o + 3] != 0f; val outRed = v[o + 4] != 0f; val bypass = v[o + 5] != 0f; val mark = v[o + 6]
        val labelH = 14.dp.toPx()
        // В окне модуля — шкала дисплея слева (0 dB у верхнего края), стержни кончаются над подписями.
        val barH = if (alignTop) size.height - labelH - 10.dp.toPx() else size.height - labelH
        if (barH <= 0) return@Canvas
        val span = if (alignTop) size.height else barH
        val barW = 9.dp.toPx()
        val cx = listOf(size.width / 6, size.width / 2, 5 * size.width / 6)
        val off = lerp(th.meterTrack, Color.White, 0.2f)
        fun yOf(db: Float) = (-db / scale * span).coerceIn(0f, barH)
        fun bar(i: Int, fill: Color, top: Float, h: Float) {
            val x = Math.round(cx[i] - barW / 2).toFloat()
            val r = CornerRadius(3.dp.toPx())
            drawRoundRect(th.meterTrack, Offset(x, 0f), Size(barW, barH), r)
            // Заливка — по форме стержня (скругление и у заливки).
            if (h > 0) clipRect(x, top, x + barW, barH) { drawRoundRect(fill, Offset(x, 0f), Size(barW, barH), r) }
        }
        fun level(red: Boolean) = if (bypass) off else if (red) th.rec else th.accent
        val yIn = if (inDb <= -scale) barH else yOf(inDb)
        val yOut = if (outDb <= -scale) barH else yOf(outDb)
        bar(0, level(inRed), yIn, barH - yIn)
        bar(2, level(outRed), yOut, barH - yOut)
        bar(1, if (bypass) off else th.gr, 0f, minOf(barH, maxOf(0f, gr) / grMax * span))
        val ry = Math.round(yOf(mark) - 1).toFloat().coerceIn(0f, barH - 2)
        val x0 = Math.round(cx[0] - barW / 2).toFloat()
        drawRect(th.text, Offset(x0 - 6.dp.toPx(), ry), Size(barW + 12.dp.toPx(), 2.dp.toPx()))
        for ((i, n) in listOf("IN", "GR", "OUT").withIndex()) {
            val t = text.measure(n, labelStyle)
            drawText(t, topLeft = Offset(cx[i] - t.size.width / 2, (if (alignTop) size.height - 4.dp.toPx() else size.height) - t.size.height))
        }
    }
}

/** Кнопка модуля мастера (MasterButtons.swift): рамка цветом модуля — модуль работает; bypass — рамки нет. Тап —
 *  окно модуля, с SHIFT — BYPASS. */
@Composable
fun MasterButton(s: ScreenState, k: Int, modifier: Modifier) {
    val th = LocalTheme.current
    val bypass = s.masterInfo.getOrElse(k) { 0 } != 0
    Box(modifier.fillMaxHeight().tile(th.panel2)
        .border(1.dp, if (bypass) Color.Transparent else moduleColor(th, k), RoundedCornerShape(Layout.radius.dp))
        .pointerTap { AppNative.masterAct(1, k); s.refresh() }, contentAlignment = Alignment.Center) {
        Text(moduleNames[k], color = th.text, fontSize = 13.tsp(), fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
    }
}

/** Кнопка точки съёма скопа (COMP, CLIP, LIM при MST на месте FX, MOD, MORPH): включённая — цветом своей волны. */
@Composable
fun ScopeButton(s: ScreenState, k: Int, modifier: Modifier) {
    val th = LocalTheme.current
    val on = s.masterInfo.getOrElse(4 + k) { 0 } != 0
    Box(modifier.fillMaxHeight().tile(if (on) moduleColor(th, k) else th.panel2)
        .pointerTap { AppNative.masterAct(2, k); s.refresh() }, contentAlignment = Alignment.Center) {
        Text(moduleNames[k], color = if (on) th.onAccent else th.text, fontSize = 13.tsp(), fontWeight = FontWeight.SemiBold,
            letterSpacing = 1.sp)
    }
}

/**
 * Скоп мастера (MasterScopeView.swift): сверху гониометр (конечный выход; точки L/R, повёрнутые на 45°, послесвечение)
 * с корреляцией под ним, ниже осциллоскоп за ~1 с (SAT белая всегда, COMP, CLIP, LIM — по кнопкам, 60 %). osc = false
 * (при MIX) — вместо осциллоскопа стерео по полосам, ниже — история громкости (MixStereoViews.kt).
 */
@Composable
fun MasterScope(s: ScreenState, modifier: Modifier, osc: Boolean = true) {
    val th = LocalTheme.current
    Column(modifier, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(Modifier.weight(0.6f).fillMaxWidth().tile(th.panel)) {
            val text = rememberTextMeasurer()
            val dim = TextStyle(color = th.dim, fontSize = 9.tsp())
            Canvas(Modifier.fillMaxSize()) { drawGonio(s.scope, th, text, dim) }
        }
        if (osc) Box(Modifier.weight(0.4f).fillMaxWidth().tile(th.panel).onSizeChanged { s.scopeWidth = maxOf(1, (it.width / density)).toInt() }) {
            Canvas(Modifier.fillMaxSize()) { drawOsc(s.scope, th) }
        }
        else {
            // При MIX на месте осциллоскопа — стерео по полосам, под ними (на месте кнопок) — история громкости.
            BandStereoView(s, Modifier.weight(0.4f).fillMaxWidth())
            LoudHistoryView(s, Modifier.height((2 * Side.pairHeight + Layout.gap).dp).fillMaxWidth())
        }
    }
}

private fun DrawScope.drawGonio(v: FloatArray, th: Theme, text: androidx.compose.ui.text.TextMeasurer, dim: TextStyle) {
    val strip = 16.dp.toPx()
    val gh = size.height - strip
    val cx = size.width / 2
    val cy = gh / 2
    val r = (minOf(size.width, gh) / 2 - 4.dp.toPx()) / 1.2f
    // Корреляция: от середины вправо — к +1 (моно), влево — к −1.
    val w = minOf(size.width - 40.dp.toPx(), 160.dp.toPx())
    val x0 = (size.width - w) / 2
    val bh = 4.5f.dp.toPx()
    val y = size.height - 6.dp.toPx() - bh
    drawRoundRect(th.meterTrack, Offset(x0, y), Size(w, bh), CornerRadius(2.dp.toPx()))
    val mid = x0 + w / 2
    val c = v.getOrElse(0) { -9f }
    if (c > -2) {
        val x = mid + c * w / 2
        drawRect(th.accent, Offset(minOf(mid, x), y), Size(maxOf(2f, kotlin.math.abs(x - mid)), bh))
    }
    drawRect(th.dim, Offset(mid - density / 2, y - 2 * density), Size(density, bh + 4 * density))
    text.measure("−1", dim).let { drawText(it, topLeft = Offset(x0 - 4 * density - it.size.width, y + bh / 2 - it.size.height / 2)) }
    text.measure("+1", dim).let { drawText(it, topLeft = Offset(x0 + w + 4 * density, y + bh / 2 - it.size.height / 2)) }
    val d = r * 0.70710678f
    val grid = th.line
    drawLine(grid, Offset(cx, cy - r), Offset(cx, cy + r), density)
    drawLine(grid, Offset(cx - r, cy), Offset(cx + r, cy), density)
    drawLine(grid, Offset(cx - d, cy - d), Offset(cx + d, cy + d), density)
    drawLine(grid, Offset(cx + d, cy - d), Offset(cx - d, cy + d), density)
    text.measure("L", dim).let { drawText(it, topLeft = Offset(cx - d - 2 * density, cy - d - 3 * density - it.size.height)) }
    text.measure("R", dim).let { drawText(it, topLeft = Offset(cx + d - 4 * density, cy - d - 3 * density - it.size.height)) }
    val n = v.getOrElse(1) { 0f }.toInt()
    if (n <= 0) return
    // Четыре части окна: старые бледнее (послесвечение).
    val parts = 4
    val per = n / parts
    val dot = 1.2f * density
    clipRect(0f, 0f, size.width, gh) {
        for (q in 0 until parts) {
            val alpha = (0.55 * Math.pow(0.75, (parts - 1 - q).toDouble())).toFloat()
            for (i in q * per until (q + 1) * per) {
                val sd = v[2 + 2 * i]
                val m = v[3 + 2 * i]
                drawRect(th.accent.copy(alpha = alpha), Offset(cx + sd * r - dot / 2, cy - m * r - dot / 2), Size(dot, dot))
            }
        }
    }
}

private fun DrawScope.drawOsc(v: FloatArray, th: Theme) {
    val cy = size.height / 2
    val amp = size.height / 2 - 2 * density
    drawRect(th.line, Offset(0f, Math.round(cy).toFloat()), Size(size.width, density))
    var p = 2 + 2 * v.getOrElse(1) { 0f }.toInt()
    for (k in 0 until 4) {
        if (p >= v.size) return
        val n = v[p].toInt()
        p += 1
        if (n <= 0) continue
        val color = moduleColor(th, k).copy(alpha = 0.6f)
        val dx = size.width / n
        for (x in 0 until n) {
            val lo = v[p + x].coerceIn(-1f, 1f)
            val hi = v[p + n + x].coerceIn(-1f, 1f)
            val top = cy - hi * amp
            val bot = cy - lo * amp
            drawRect(color, Offset(x * dx, top), Size(maxOf(dx, 1f), maxOf(density, bot - top)))
        }
        p += 2 * n
    }
}
