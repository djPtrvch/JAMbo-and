package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative

/**
 * Миниатюры LFO1–3 и ENV в окне звука, пока держат MOD (ModPreview.swift ветки swift JAMbo): LFO — период формы
 * с Polarity и Invert и точка на текущей фазе; ENV — форма ADSR по ручкам и точка на кривой. Кривые — из C++ (modMini).
 */
@Composable
fun ModPreview(modifier: Modifier) {
    val th = LocalTheme.current
    var data by remember { mutableStateOf(AppNative.modMinis()) }
    var titles by remember { mutableStateOf(AppNative.modMiniTitles()) }
    LaunchedEffect(Unit) {
        while (true) withFrameNanos {
            data = AppNative.modMinis()
            titles = AppNative.modMiniTitles()
        }
    }
    // Разбор: по миниатюре [n, x n, y n, точка x, y].
    val minis = ArrayList<Triple<FloatArray, FloatArray, Offset>>()
    var i = 0
    while (i < data.size && minis.size < 4) {
        val n = data[i].toInt()
        val xs = data.copyOfRange(i + 1, i + 1 + n)
        val ys = data.copyOfRange(i + 1 + n, i + 1 + 2 * n)
        minis.add(Triple(xs, ys, Offset(data[i + 1 + 2 * n], data[i + 2 + 2 * n])))
        i += 3 + 2 * n
    }
    Column(modifier.fillMaxSize().padding(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        for (k in minis.indices) {
            val (xs, ys, dot) = minis[k]
            val env = k == 3
            Column(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(titles.getOrElse(k) { "" }, color = th.dim, fontSize = 10.sp, maxLines = 1)
                Canvas(Modifier.weight(1f).fillMaxWidth()) {
                    val h = size.height
                    val pad = 3.dp.toPx()
                    fun y(v: Float) = if (env) h - pad - v * (h - 2 * pad) else h / 2 - v * (h / 2 - pad)
                    if (!env) {
                        drawLine(th.off, Offset(0f, h / 2), Offset(size.width, h / 2), 1.dp.toPx(),
                            pathEffect = PathEffect.dashPathEffect(floatArrayOf(3.dp.toPx(), 3.dp.toPx())))
                    }
                    val p = Path()
                    for (j in xs.indices) {
                        val x = xs[j] * size.width
                        if (j == 0) p.moveTo(x, y(ys[j])) else p.lineTo(x, y(ys[j]))
                    }
                    drawPath(p, th.text, style = Stroke(2.dp.toPx(), join = StrokeJoin.Round))
                    drawCircle(th.accent, 3.dp.toPx(), Offset(dot.x * size.width, y(dot.y)))
                }
            }
        }
    }
}
