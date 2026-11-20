package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import java.util.Locale
import kotlin.math.log10
import kotlin.math.max


private const val VOL_MIN = -60.0
private const val TRAVEL = 240f
private const val BAR_W = 9f
private const val BAR_GAP = 3f
private const val OVER = 0.999f

/**
 * Измеритель уровня справа (MeterColumn.swift ветки swift JAMbo): сверху интегральная громкость LUFS (тап — сброс) и
 * метка клиппинга, полосы (стерео — две, моно — одна; −60…0 dB, пик-холд, выше 0 dB — красная, заглушённая — серая),
 * снизу наибольший пик (тап — сброс). Поверх полос — регулятор громкости: у дорожки — её громкость в микшере, при MIX
 * и MST — громкость выхода (палец вверх-вниз; пока палец на нём — снизу VOL). Данные — каждый кадр из C++.
 */
@Composable
fun MeterColumn(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    var m by remember { mutableStateOf(AppNative.meter(0.0)) }
    LaunchedEffect(Unit) {
        while (true) withFrameNanos { m = AppNative.meter(it / 1e9) }
    }
    val n = m[3].toInt()
    val at = 4 + 2 * n
    val clip = m[at] != 0f
    val lufs = m[at + 1]
    val peak = m[at + 2]
    val vol = m[at + 3].toDouble()
    val drag = m[at + 4] != 0f
    val cur by rememberUpdatedState(vol)
    Column(modifier.width(Layout.meterWidth.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Label("LUFS\n" + if (lufs.isFinite()) String.format(Locale.US, "%.1f", lufs) else "−∞") { s.act(Act.RESET_LOUDNESS) }
        Box(Modifier.fillMaxWidth(), contentAlignment = androidx.compose.ui.Alignment.Center) {
            Box(Modifier.width(21.dp).height(4.dp).background(if (clip) th.rec else th.off, RoundedCornerShape(2.dp)))
        }
        Canvas(Modifier.weight(1f).fillMaxWidth().pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                val start = cur
                val y0 = down.position.y
                s.act(Act.VOL_DRAG, 1)
                while (true) {
                    val ev = awaitPointerEvent()
                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                    if (!c.pressed) break
                    val dy = -(c.position.y - y0) / density / TRAVEL
                    s.act(Act.SET_METER_VOL, 0, 0, start + dy * -VOL_MIN)
                    c.consume()
                }
                s.act(Act.VOL_DRAG, 0)
            }
        }) {
            drawBars(m, n, m[2] != 0f, th)
            // Регулятор громкости — белая планка по той же шкале.
            val f = ((vol - VOL_MIN) / -VOL_MIN).toFloat().coerceIn(0f, 1f)
            val tw = 30.dp.toPx()
            val thh = 8.dp.toPx()
            val y = size.height * (1 - f) - thh / 2
            val x = (size.width - tw) / 2
            drawRoundRect(th.text, Offset(x, y), Size(tw, thh), CornerRadius(3.dp.toPx()))
            drawRoundRect(th.bg, Offset(x, y), Size(tw, thh), CornerRadius(3.dp.toPx()), style = Stroke(1.5.dp.toPx()))
        }
        val bottom = if (drag) "VOL\n" + (if (vol <= VOL_MIN + 0.05) "−∞" else String.format(Locale.US, "%.1f", vol))
        else (if (peak > 0.001f) String.format(Locale.US, "%.1f", 20 * log10(peak)) else "−∞") + "\ndB"
        Label(bottom) { s.act(Act.RESET_METER_PEAK) }
    }
}

@Composable
private fun Label(text: String, onTap: () -> Unit) {
    val th = LocalTheme.current
    Text(text, color = th.dim, fontSize = 9.tsp(), lineHeight = 10.tsp(), textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth().pointerTap(onTap))
}

/** Пик (0…1+) → доля высоты полосы (шкала −60…0 dBFS). */
private fun frac(peak: Float): Float {
    if (peak <= 0) return 0f
    return ((20 * log10(peak) + 60) / 60).coerceIn(0f, 1f)
}

/** Полосы в общем стиле: 9 точек, скругление 3, зазор 3; заливка жёлтая, выше 0 dBFS — красная, заглушённая —
 *  серая; пик-холд — черта цвета текста 2 точки. */
private fun DrawScope.drawBars(m: FloatArray, n: Int, muted: Boolean, th: Theme) {
    val w = BAR_W.dp.toPx()
    val gap = BAR_GAP.dp.toPx()
    val total = n * w + (n - 1) * gap
    val h = size.height
    val rad = CornerRadius(3.dp.toPx())
    for (k in 0 until n) {
        val level = m[4 + 2 * k]
        val hold = m[5 + 2 * k]
        val x = (size.width - total) / 2 + k * (w + gap)
        val shape = Path().apply { addRoundRect(androidx.compose.ui.geometry.RoundRect(x, 0f, x + w, h, rad)) }
        drawPath(shape, th.meterTrack)
        clipPath(shape) {
            val f = frac(level)
            val fill = if (muted) th.muteGrey else if (level >= OVER) th.rec else th.accent
            if (f > 0) drawRect(fill, Offset(x, h - f * h), Size(w, f * h))
            val hf = frac(hold)
            if (hf > 0.01f) drawRect(th.text, Offset(x, max(0f, h - hf * h - 1.dp.toPx())), Size(w, 2.dp.toPx()))
        }
    }
}
