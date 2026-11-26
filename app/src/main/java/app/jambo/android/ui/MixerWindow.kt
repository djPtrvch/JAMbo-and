package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import java.util.Locale
import kotlin.math.abs
import kotlin.math.log10

private const val STRIDE = 10
private const val LABEL_H = 24f
private const val GAP = 4f
private const val CLIP_H = 4f
private const val VOL_MIN = -60.0

/**
 * Окно микшера при MIX (MixerWindow.swift ветки swift JAMbo): тап по обзору полос слева открывает, двойной тап по MIX
 * закрывает. На месте центра; над каждой дорожкой строки дорожек — фейдер как измеритель справа: сверху громкость (ручка
 * MIX дорожки), метка клиппинга, полосы (стерео — две, моно — одна, пик-холд, Sidechain — оранжевым сверху) с белой
 * планкой, снизу наибольший пик (тап — сброс). Палец вверх-вниз — громкость (как ручка MIX: с зажатыми шагами — p-lock).
 */
@Composable
fun MixerWindow(s: ScreenState, modifier: Modifier) {
    val text = rememberTextMeasurer()
    var data by remember { mutableStateOf(FloatArray(1)) }
    var frame by remember { mutableStateOf(Rect.Zero) }
    LaunchedEffect(Unit) { while (true) withFrameNanos { data = AppNative.mixerData(it / 1e9) } }
    // Середины дорожек строки дорожек (MIX | 1…N | MST, равные места) в координатах окна, px.
    fun centers(n: Int, density: Float): List<Float> {
        val row = s.tracksRect
        if (n == 0 || row.width <= 0 || frame.width <= 0) return emptyList()
        val g = Layout.gap * density
        val side = Layout.step(row.width / density) * density
        val w = (row.width - 2 * side - (n + 1) * g) / n
        return (0 until n).map { row.left + side + g + it * (w + g) + w / 2 - frame.left }
    }
    Box(modifier) {
        MixerFaders(s, Modifier.fillMaxSize(), data, text, ::centers) { frame = it }
        if (s.midi.controls.learning) {
            val density = LocalDensity.current.density
            val n = data.getOrElse(0) { 0f }.toInt()
            for ((i, x) in centers(n, density).withIndex()) {
                Box(Modifier.offset(x = (x / density - 22).dp).width(44.dp).fillMaxHeight()
                    .midiLearnMark(s, s.midi.controls.knobTarget(i)))
            }
        }
    }
}

@Composable
private fun MixerFaders(
    s: ScreenState, modifier: Modifier, data: FloatArray, text: TextMeasurer,
    centers: (Int, Float) -> List<Float>, positioned: (Rect) -> Unit,
) {
    val th = LocalTheme.current
    val currentData by androidx.compose.runtime.rememberUpdatedState(data)
    val currentCenters by androidx.compose.runtime.rememberUpdatedState(centers)
    Canvas(modifier.onGloballyPositioned { c ->
        s.body?.let { b -> if (b.isAttached) positioned(b.localBoundingBoxOf(c)) }
    }
        .pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown()
                val n = currentData.getOrElse(0) { 0f }.toInt()
                val xs = currentCenters(n, density)
                val i = xs.indices.minByOrNull { abs(xs[it] - down.position.x) } ?: return@awaitEachGesture
                if (abs(xs[i] - down.position.x) > 22 * density) return@awaitEachGesture
                if (s.learnTarget(s.midi.controls.knobTarget(i))) {
                    down.consume()
                    return@awaitEachGesture
                }
                val top = (LABEL_H + GAP + CLIP_H + GAP) * density
                val barsH = size.height - top - (GAP + LABEL_H) * density
                AppNative.act(Act.BEGIN_GESTURE, 0, 0, 0.0)
                val start = AppNative.act(Act.KNOB_START, i, 0, 0.0)
                var moved = false
                while (true) {
                    val ev = awaitPointerEvent()
                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                    val d = c.position - down.position
                    if (abs(d.y) >= 4 * density || abs(d.x) >= 4 * density) moved = true
                    if (moved) AppNative.act(Act.SET_KNOB, i, 0, start - d.y / density / 240.0)
                    c.consume()
                    if (!c.pressed) break
                }
                AppNative.act(Act.END_GESTURE, 0, 0, 0.0)
                if (!moved && down.position.y >= top + barsH + GAP * density) AppNative.mixerResetPeak(i)
                s.refresh()
            }
        }) {
        val n = data.getOrElse(0) { 0f }.toInt()
        val xs = centers(n, density)
        if (xs.isEmpty()) return@Canvas
        val top = (LABEL_H + GAP + CLIP_H + GAP) * density
        val h = maxOf(1f, size.height - top - (GAP + LABEL_H) * density)
        val sx = s.tracksRect.left + Layout.step(s.tracksRect.width / density) * density / 2 - frame.left
        scaleMarks(sx, top, h, th, text)
        val dim = TextStyle(color = th.dim, fontSize = (9 * canvasTextScale).sp2())
        val vol = TextStyle(color = th.text.copy(alpha = 0.75f), fontSize = (9 * canvasTextScale).sp2(), fontWeight = FontWeight.Medium)
        val peakY = (top + h + size.height + Layout.gap * density) / 2
        for (i in 0 until n) {
            val o = 1 + STRIDE * i
            if (o + STRIDE > data.size) break
            val x = xs[i]
            val stereo = data[o] != 0f
            val muted = data[o + 1] != 0f
            val bars = if (stereo) listOf(data[o + 2] to data[o + 3], data[o + 4] to data[o + 5]) else listOf(data[o + 2] to data[o + 3])
            val bw = 9 * density
            val span = bars.size * bw + (bars.size - 1) * 3 * density
            val duck = data[o + 6]
            val gr = if (duck <= -0.1f) -duck / 24 else 0f
            for ((k, b) in bars.withIndex()) bar(x - span / 2 + k * (bw + 3 * density), top, bw, h, b.first, b.second, muted, gr, th)
            drawRoundRect(if (data[o + 7] != 0f) th.rec else th.off, Offset(x - 10.5f * density, (LABEL_H + GAP) * density),
                Size(21 * density, CLIP_H * density), CornerRadius(2 * density))
            val v = data[o + 9].toDouble()
            val f = ((v - VOL_MIN) / -VOL_MIN).toFloat().coerceIn(0f, 1f)
            val ty = top + h - h * f
            drawRoundRect(th.text, Offset(x - 15 * density, ty - 4 * density), Size(30 * density, 8 * density), CornerRadius(3 * density))
            drawRoundRect(th.bg, Offset(x - 15 * density, ty - 4 * density), Size(30 * density, 8 * density), CornerRadius(3 * density),
                style = Stroke(1.5f * density))
            centered(text, if (v <= VOL_MIN + 0.05) "−∞" else String.format(Locale.US, "%.1f", v), vol, x, 14 * density)
            val pk = data[o + 8]
            centered(text, if (pk > 0.001f) String.format(Locale.US, "%.1f", 20 * log10(pk)) else "−∞", dim, x, peakY)
        }
        centered(text, "VOL", vol, sx, 14 * density)
        centered(text, "PEAK", dim, sx, peakY)
    }
}

private fun Float.sp2() = androidx.compose.ui.unit.TextUnit(this, androidx.compose.ui.unit.TextUnitType.Sp)

private fun DrawScope.centered(text: TextMeasurer, s: String, style: TextStyle, x: Float, y: Float) {
    val t = text.measure(s, style)
    drawText(t, topLeft = Offset(x - t.size.width / 2, y - t.size.height / 2))
}

/** Шкала dB слева (над MIX): 0…−60 через 6 dB, подпись и короткая черта справа. */
private fun DrawScope.scaleMarks(mid: Float, top: Float, h: Float, th: Theme, text: TextMeasurer) {
    var db = 0
    val st = TextStyle(color = th.dim, fontSize = (9 * canvasTextScale).sp2())
    while (db >= -60) {
        val y = top + h * -db / 60f
        drawLine(th.line, Offset(mid + 6 * density, y), Offset(mid + 14 * density, y), density)
        val t = text.measure(if (db == 0) "0" else "−${-db}", st)
        drawText(t, topLeft = Offset(mid + 2 * density - t.size.width, y - t.size.height / 2))
        db -= 6
    }
}

/** Полоса фейдера: −60…0 dBFS, выше 0 — красная, заглушённая — серая, пик-холд, Sidechain — оранжевым сверху. */
private fun DrawScope.bar(x: Float, top: Float, w: Float, h: Float, level: Float, hold: Float, muted: Boolean, gr: Float, th: Theme) {
    fun frac(p: Float) = if (p <= 0) 0f else ((20 * log10(p) + 60) / 60).coerceIn(0f, 1f)
    val shape = Path().apply { addRoundRect(RoundRect(x, top, x + w, top + h, CornerRadius(3 * density))) }
    drawPath(shape, th.meterTrack)
    clipPath(shape) {
        val f = frac(level)
        val fill = if (muted) th.muteGrey else if (level >= 0.999f) th.rec else th.accent
        if (f > 0) drawRect(fill, Offset(x, top + h - f * h), Size(w, f * h))
        val hf = frac(hold)
        if (hf > 0.01f) drawRect(th.text, Offset(x, maxOf(top, top + h - hf * h - density)), Size(w, 2 * density))
        if (gr > 0) drawRect(th.gr, Offset(x, top), Size(w, minOf(1f, gr) * h))
    }
}
