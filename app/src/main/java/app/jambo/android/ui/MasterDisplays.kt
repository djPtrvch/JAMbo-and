package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import app.jambo.android.MasterWinAct
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.log10
import kotlin.math.log2
import kotlin.math.pow

/** Шкала дисплеев окон модулей мастера, dB (SCALE в SETTINGS — позже), сетка — восьмая часть; отступ 0 dB сверху. */
private const val SCALE = 48f
private const val GRID = 6
private const val TOP_INSET = 12f
private const val LINE_HIT = 22f

/** Данные masterDisplay по частям (COMP, CLIP, LIM). */
private class Tele(v: FloatArray) {
    val cols = v.getOrElse(0) { 0f }.toInt()
    val inDb = slice(v, 1, cols)
    val outDb = slice(v, 1 + cols, cols)
    val gr = slice(v, 1 + 2 * cols, cols)
    private val o = 1 + 3 * cols
    val inPeak = v.getOrElse(o) { -999f }
    val det = v.getOrElse(o + 1) { -999f }
    val grNow = v.getOrElse(o + 2) { -999f }
    val live = inPeak > -998
    val tn = v.getOrElse(o + 3) { 0f }.toInt()
    val curve = slice(v, o + 4, tn)
    val guard = v.getOrElse(o + 4 + tn) { 0f }
}

private fun slice(v: FloatArray, at: Int, n: Int): FloatArray =
    if (n <= 0 || at + n > v.size) FloatArray(0) else v.copyOfRange(at, at + n)

private fun yOf(db: Float, h: Float) = (-db / SCALE * h).coerceIn(0f, h)
private fun xOf(db: Float, w: Float) = ((1 + db / SCALE) * w).coerceIn(0f, w)

/**
 * Дисплей окна модуля (MasterDisplay.swift и окна модулей ветки swift JAMbo): COMP и CLIP — LEVEL на всю ширину и
 * поверх слева TRANSFER, справа блок стержней; LIM — LEVEL и блок; SAT — TRANSFER и SPECTRUM. Двойной тап мимо
 * линии — FREEZE.
 */
@Composable
fun MasterDisplay(s: ScreenState, m: Int, modifier: Modifier) {
    BoxWithConstraints(modifier) {
        val w = maxWidth.value
        val h = maxHeight.value
        if (m == 0) {
            val tw = minOf(w * 0.35f, h)
            s.transferCols = tw.toInt()
            s.dispCols = maxOf(2, ((w - tw) / 3).toInt() + 1)
            Row(Modifier.fillMaxSize()) {
                SatTransfer(s, Modifier.width(tw.dp).fillMaxHeight())
                SatSpectrum(s, Modifier.weight(1f).fillMaxHeight())
            }
            return@BoxWithConstraints
        }
        val block = 96f
        val plotW = w - block - Layout.gap
        val tw = minOf(plotW * 0.4f, h * 1.4f)
        s.dispCols = maxOf(1, plotW.toInt())
        s.transferCols = maxOf(2, tw.toInt())
        Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            Box(Modifier.weight(1f).fillMaxHeight()) {
                LevelPlot(s, m, Modifier.fillMaxSize())
                if (m == 1) CompTransfer(s, Modifier.width(tw.dp).fillMaxHeight())
                if (m == 2) ClipTransfer(s, Modifier.width(tw.dp).fillMaxHeight())
            }
            DynBlock(s, m, Modifier.width(block.dp).fillMaxHeight())
        }
    }
}

private fun param(id: String) = AppNative.masterParam(id, 2, 0.0).toFloat()

/** Жест с началом и концом (одна запись UNDO): begin — по точке касания решает, тянется ли что-то. */
private suspend fun PointerInputScope.dragGesture(
    begin: (Offset) -> Boolean, move: (Offset, Offset) -> Unit, tapped: () -> Unit,
) {
    awaitEachGesture {
        val down = awaitFirstDown()
        val grab = begin(down.position)
        if (grab) AppNative.act(Act.BEGIN_GESTURE, 0, 0, 0.0)
        var moved = false
        while (true) {
            val ev = awaitPointerEvent()
            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
            val d = c.position - down.position
            if (abs(d.x) > 6 * density || abs(d.y) > 6 * density) moved = true
            if (grab) move(d, c.position)
            c.consume()
            if (!c.pressed) break
        }
        if (grab) AppNative.act(Act.END_GESTURE, 0, 0, 0.0) else if (!moved) tapped()
    }
}

/** Двойной тап (второй за 0,32 с) — FREEZE. */
private class DoubleTap {
    var last = 0L
    fun tap(): Boolean {
        val now = System.currentTimeMillis()
        val ok = now - last < 320
        last = if (ok) 0 else now
        return ok
    }
}

/** Дисплей уровня за 4 с (LEVEL компрессора и клиппера, дисплей лимитера): линия THRESHOLD или CEILING тянется пальцем. */
@Composable
private fun LevelPlot(s: ScreenState, m: Int, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val lineId = when (m) { 1 -> "cmp.threshold"; 2 -> "clip.ceiling"; else -> "lim.ceiling" }
    val dt = DoubleTap()
    var start = 0.0
    Box(modifier.clip(RoundedCornerShape(Layout.radius.dp)).background(th.panel).padding(top = TOP_INSET.dp)) {
        Canvas(Modifier.fillMaxSize().pointerInput(m) {
            dragGesture({ p ->
                val v = AppNative.masterParam(lineId, 2, 0.0)
                start = v
                abs(p.y - yOf(v.toFloat(), size.height.toFloat())) <= LINE_HIT * density
            }, { d, _ ->
                AppNative.masterParam(lineId, 3, start - d.y / size.height * SCALE)
                s.refresh()
            }) { if (dt.tap()) { AppNative.masterWinAct(MasterWinAct.FREEZE, 0, 0, ""); s.refresh() } }
        }) {
            val t = Tele(s.masterDisp)
            if (t.cols <= 0) return@Canvas
            val bypass = s.masterWin[2] != 0
            grid(th, text, true)
            val h = size.height
            val sx = size.width / t.cols
            drawPath(levelPath(t.inDb, h, sx, true), th.dim.copy(alpha = 0.35f))
            drawPath(levelPath(t.inDb, h, sx, false), th.dim, style = Stroke(density))
            val outColor = when (m) { 1 -> th.accent; 2 -> th.gr; else -> th.rec }
            val outFill = when (m) { 3 -> 0.2f; else -> 0.15f }
            if (m != 3 || !bypass) {
                drawPath(levelPath(t.outDb, h, sx, true), outColor.copy(alpha = outFill))
                drawPath(levelPath(t.outDb, h, sx, false), outColor, style = Stroke(1.5f * density))
            }
            if (m == 3) overMarks(t.inDb, sx, th)
            if (!bypass) {
                val grAlpha = when (m) { 1 -> 0.18f; 2 -> 0.25f; else -> 1f }
                drawPath(grPath(t.gr, h, sx), th.gr.copy(alpha = grAlpha))
                if (m == 1) drawPath(grLine(t.gr, h, sx), th.gr, style = Stroke(1.5f * density))
                grPeaks(t.gr, sx, if (m == 3) 80.0 else 100.0, th, text)
            }
            val lv = param(lineId)
            if (m == 1) thresholdLine(lv, th, text) else ceilingLine(lv, th, text)
            if (m == 3 && !bypass) {
                val lim = lv + 0.1f
                for (x in t.outDb.indices) if (t.outDb[x] > lim) drawCircle(th.rec, 3 * density, Offset(x * sx, 4 * density))
                if (t.guard >= 0.1f) label(text, "GUARD -%.1f dB".format(t.guard), th.gr, 30f, if (s.masterWin[6] != 0) 22f else 6f, true)
            }
            if (m != 3) title(text, "LEVEL", th)
            if (s.masterWin[6] != 0) label(text, "FREEZE", th.accent, 30f, 6f, true)
        }
    }
}

private fun DrawScope.grid(th: Theme, text: TextMeasurer, labels: Boolean) {
    var db = 0
    while (-db < SCALE) {
        val y = Math.round(-db / SCALE * size.height).toFloat()
        drawRect(th.line, Offset(0f, y), Size(size.width, density))
        if (labels) {
            val t = text.measure(if (db == 0) "0" else "$db", TextStyle(color = th.dim, fontSize = (10 * canvasTextScale).sp()))
            drawText(t, topLeft = Offset(4 * density, y + 2 * density))
        }
        db -= GRID
    }
}

private fun Float.sp() = androidx.compose.ui.unit.TextUnit(this, androidx.compose.ui.unit.TextUnitType.Sp)

private fun DrawScope.label(text: TextMeasurer, s: String, color: Color, x: Float, y: Float, bold: Boolean = false) {
    val t = text.measure(s, TextStyle(color = color, fontSize = (11 * canvasTextScale).sp(), fontWeight = if (bold) FontWeight.SemiBold else null))
    drawText(t, topLeft = Offset(x * density, y * density))
}

private fun DrawScope.title(text: TextMeasurer, s: String, th: Theme) {
    val t = text.measure(s, TextStyle(color = th.dim, fontSize = (9 * canvasTextScale).sp()))
    drawText(t, topLeft = Offset(size.width - 6 * density - t.size.width, size.height - 4 * density - t.size.height))
}

private fun levelPath(data: FloatArray, h: Float, sx: Float, closed: Boolean): Path {
    val p = Path()
    if (closed) p.moveTo(0f, h)
    for (x in data.indices) {
        val y = yOf(data[x], h)
        if (x == 0 && !closed) p.moveTo(0f, y) else p.lineTo(x * sx, y)
    }
    if (closed) {
        p.lineTo(data.size * sx, h)
        p.close()
    }
    return p
}

private fun grPath(gr: FloatArray, h: Float, sx: Float): Path {
    val p = Path()
    p.moveTo(0f, 0f)
    for (x in gr.indices) {
        val y = minOf(h, gr[x] / SCALE * h)
        p.lineTo(x * sx, y)
        p.lineTo((x + 1) * sx, y)
    }
    p.lineTo(gr.size * sx, 0f)
    p.close()
    return p
}

private fun grLine(gr: FloatArray, h: Float, sx: Float): Path {
    val p = Path()
    for (x in gr.indices) {
        val y = minOf(h, gr[x] / SCALE * h)
        if (x == 0) p.moveTo(0f, y) else p.lineTo(x * sx, y)
    }
    return p
}

private fun DrawScope.overMarks(inDb: FloatArray, sx: Float, th: Theme) {
    for (x in inDb.indices) if (inDb[x] > 0) drawRect(th.text.copy(alpha = 0.7f), Offset(x * sx, 0f), Size(sx, 3 * density))
}

/** Подписи пиков GR: локальные максимумы ≥ 1 dB, не чаще gapMs, до 6 самых сильных. */
private fun DrawScope.grPeaks(gr: FloatArray, sx: Float, gapMs: Double, th: Theme, text: TextMeasurer) {
    val cols = gr.size
    if (cols < 3) return
    val gapCols = maxOf(1, Math.round(gapMs / 1000 / 4 * cols).toInt())
    val peaks = ArrayList<Pair<Int, Float>>()
    for (x in 1 until cols - 1) {
        val v = gr[x]
        if (v >= 1 && v >= gr[x - 1] && v >= gr[x + 1]) {
            val last = peaks.lastOrNull()
            if (last != null && x - last.first < gapCols) {
                if (v > last.second) peaks[peaks.size - 1] = x to v
            } else peaks.add(x to v)
        }
    }
    for ((x, v) in peaks.sortedByDescending { it.second }.take(6)) {
        val y = minOf(size.height - 12 * density, v / SCALE * size.height + 3 * density)
        val t = text.measure("-%.1f".format(v), TextStyle(color = th.gr, fontSize = (10 * canvasTextScale).sp()))
        val px = x * sx
        drawText(t, topLeft = Offset(if (px < 34 * density) px + 6 * density else px - 6 * density - t.size.width, y))
    }
}

private fun DrawScope.ceilingLine(db: Float, th: Theme, text: TextMeasurer) {
    val cy = Math.round(yOf(db, size.height)).toFloat()
    drawRect(th.text, Offset(0f, cy - density), Size(size.width, 2 * density))
    numberAt(db, cy, th, text)
}

private fun DrawScope.thresholdLine(db: Float, th: Theme, text: TextMeasurer) {
    val cy = yOf(db, size.height)
    drawLine(th.text, Offset(0f, cy), Offset(size.width, cy), density, pathEffect = PathEffect.dashPathEffect(floatArrayOf(4 * density, 4 * density)))
    numberAt(db, cy, th, text)
}

private fun DrawScope.numberAt(db: Float, cy: Float, th: Theme, text: TextMeasurer) {
    val t = text.measure("%.1f".format(db), TextStyle(color = th.text, fontSize = (11 * canvasTextScale).sp()))
    val below = cy <= 16 * density
    drawText(t, topLeft = Offset(size.width - 6 * density - t.size.width, if (below) cy + 3 * density else cy - 3 * density - t.size.height))
}

/** Подложка TRANSFER поверх LEVEL: тёмная полупрозрачная, справа — граница линией. */
@Composable
private fun TransferBox(modifier: Modifier, content: @Composable () -> Unit) {
    val th = LocalTheme.current
    Box(modifier.clip(RoundedCornerShape(topStart = Layout.radius.dp, bottomStart = Layout.radius.dp)).background(th.bg.copy(alpha = 0.5f))) {
        Box(Modifier.fillMaxSize().padding(top = TOP_INSET.dp)) { content() }
        Box(Modifier.width(1.dp).fillMaxHeight().background(th.line).align(androidx.compose.ui.Alignment.CenterEnd))
    }
}

private fun DrawScope.curvePath(curve: FloatArray, from: Int, to: Int): Path {
    val p = Path()
    val n = curve.size
    if (n < 2) return p
    for (i in from..minOf(to, n - 1)) {
        val x = i.toFloat() / (n - 1) * size.width
        val y = yOf(curve[i], size.height)
        if (i == from) p.moveTo(x, y) else p.lineTo(x, y)
    }
    return p
}

private fun DrawScope.handle(c: Offset, th: Theme, text: TextMeasurer?, label: String?) {
    val r = 13 * density
    drawCircle(th.panel, r, c)
    drawCircle(th.text, r, c, style = Stroke(1.5f * density))
    if (label != null && text != null) {
        val t = text.measure(label, TextStyle(color = th.text, fontSize = (9 * canvasTextScale).sp()))
        drawText(t, topLeft = Offset(c.x - t.size.width / 2, c.y - t.size.height / 2))
    } else drawCircle(th.text, 3 * density, c)
}

/** TRANSFER компрессора: кривая, колено, порог (точка тянется вдоль диагонали), кружок RATIO, живая точка детектора. */
@Composable
private fun CompTransfer(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    var kind = 0
    var start = 0.0
    TransferBox(modifier) {
        Canvas(Modifier.fillMaxSize().pointerInput(Unit) {
            dragGesture({ p ->
                val thr = param("cmp.threshold")
                val ratio = param("cmp.ratio")
                val w = size.width.toFloat()
                val h = size.height.toFloat()
                val out0 = thr + (0 - thr) / maxOf(1f, ratio)
                val hp = Offset(w - 14 * density - 2 * density, yOf(out0, h))
                val tp = Offset(xOf(thr, w), yOf(thr, h))
                kind = when {
                    hypot(p.x - hp.x, p.y - hp.y) <= LINE_HIT * density -> 2
                    hypot(p.x - tp.x, p.y - tp.y) <= (LINE_HIT + 6) * density -> 1
                    else -> 0
                }
                start = if (kind == 2) hp.y.toDouble() else thr.toDouble()
                kind > 0
            }, { d, _ ->
                val w = size.width.toDouble()
                val h = size.height.toDouble()
                if (kind == 1) {
                    val dx = d.x / w * SCALE
                    val dy = -d.y / h * SCALE
                    AppNative.masterParam("cmp.threshold", 3, start + if (abs(dx) >= abs(dy)) dx else dy)
                } else if (kind == 2) {
                    val thr = AppNative.masterParam("cmp.threshold", 2, 0.0)
                    if (-thr >= 0.5) {
                        val out0 = minOf(0.0, -((start + d.y) / h) * SCALE)
                        AppNative.masterParam("cmp.ratio", 3, -thr / maxOf(1e-3, out0 - thr))
                    }
                }
                s.refresh()
            }) { }
        }) {
            val t = Tele(s.masterDisp)
            val w = size.width
            val h = size.height
            val thr = param("cmp.threshold")
            val ratio = param("cmp.ratio")
            val knee = param("cmp.knee")
            val bypass = s.masterWin[2] != 0
            transferGridSafe(th)
            if (knee > 0) {
                val a = xOf(thr - knee / 2, w)
                val b = xOf(thr + knee / 2, w)
                drawRect(th.dim.copy(alpha = 0.35f), Offset(a, 0f), Size(b - a, h))
            }
            drawPath(curvePath(t.curve, 0, t.curve.size), if (bypass) th.dim else th.text, style = Stroke(1.5f * density))
            if (!bypass && t.live && t.curve.size > 1) {
                val n = t.curve.size
                fun grAt(i: Int): Float { val l = -SCALE + i.toFloat() / (n - 1) * SCALE; return l - t.curve[i] }
                if (t.grNow > 0.05f) {
                    val a = maxOf(0, (xOf(thr - knee / 2, w) / w * (n - 1)).toInt())
                    var b = a
                    while (b < n - 1 && grAt(b) < t.grNow) b++
                    drawPath(curvePath(t.curve, a, b), th.gr, style = Stroke(2.5f * density))
                }
                if (t.det > -SCALE) {
                    val i = ((t.det + SCALE) / SCALE * (n - 1)).toInt().coerceIn(0, n - 1)
                    drawCircle(th.text, 3 * density, Offset(xOf(t.det, w), yOf(t.curve[i], h)))
                }
            }
            val tx = xOf(thr, w)
            val ty = yOf(thr, h)
            val dash = PathEffect.dashPathEffect(floatArrayOf(4 * density, 4 * density))
            drawLine(th.text, Offset(tx, 0f), Offset(tx, h), density, pathEffect = dash)
            drawLine(th.text, Offset(0f, ty), Offset(w, ty), density, pathEffect = dash)
            drawCircle(th.text, 5 * density, Offset(tx, ty))
            val out0 = thr + (0 - thr) / maxOf(1f, ratio)
            handle(Offset(w - 16 * density, yOf(out0, h)), th, text, if (ratio < 9.95f) "%.1f".format(ratio) else "${Math.round(ratio)}")
            title(text, "TRANSFER", th)
        }
    }
}

private fun DrawScope.transferGridSafe(th: Theme) {
    var db = 0
    while (-db < SCALE) {
        val y = Math.round(-db / SCALE * size.height).toFloat()
        drawRect(th.line, Offset(0f, y), Size(size.width, density))
        db -= GRID
    }
    db = -GRID
    while (-db < SCALE) {
        val x = Math.round(xOf(db.toFloat(), size.width)).toFloat()
        drawRect(th.line, Offset(x, 0f), Size(density, size.height))
        db -= GRID
    }
    drawLine(th.dim, Offset(0f, size.height), Offset(size.width, 0f), density)
}

/** TRANSFER клиппера: колено, кривая, пунктир CEILING, ручка-угол (CEILING = y, INPUT = CEILING − x), живая точка. */
@Composable
private fun ClipTransfer(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    TransferBox(modifier) {
        Canvas(Modifier.fillMaxSize().pointerInput(Unit) {
            dragGesture({ p ->
                val c = Offset(xOf(param("clip.ceiling") - param("clip.input"), size.width.toFloat()),
                    yOf(param("clip.ceiling"), size.height.toFloat()))
                hypot(p.x - c.x, p.y - c.y) <= LINE_HIT * density
            }, { _, pos ->
                val x = -SCALE + pos.x / maxOf(1, size.width) * SCALE
                val y = -pos.y / maxOf(1, size.height) * SCALE
                AppNative.masterParam("clip.ceiling", 3, y.toDouble())
                val ceil = AppNative.masterParam("clip.ceiling", 2, 0.0)
                AppNative.masterParam("clip.input", 3, ceil - x)
                s.refresh()
            }) { }
        }) {
            val t = Tele(s.masterDisp)
            val w = size.width
            val h = size.height
            val input = param("clip.input")
            val ceil = param("clip.ceiling")
            val soft = param("clip.softness")
            val bypass = s.masterWin[2] != 0
            transferGridSafe(th)
            val xc = ceil - input
            val k = 1 - 0.6f * (soft / 100)
            val xk = xc + 20 * log10(k)
            val a = xOf(xk, w)
            val b = xOf(xc, w)
            drawRect(th.dim.copy(alpha = 0.35f), Offset(a, 0f), Size(b - a, h))
            drawPath(curvePath(t.curve, 0, t.curve.size), if (bypass) th.dim else th.text, style = Stroke(1.5f * density))
            val ty = yOf(ceil, h)
            drawLine(th.text, Offset(0f, ty), Offset(w, ty), density, pathEffect = PathEffect.dashPathEffect(floatArrayOf(4 * density, 4 * density)))
            val n = t.curve.size
            if (!bypass && t.live && t.inPeak > 0 && n > 1) {
                val x = 20 * log10(t.inPeak) - input
                if (x > -SCALE) {
                    if (t.grNow > 0.05f && x > xk) {
                        val i0 = maxOf(0, (xOf(xk, w) / w * (n - 1)).toInt())
                        val i1 = minOf(n - 1, kotlin.math.ceil(xOf(x, w) / w * (n - 1)).toInt())
                        if (i1 > i0) drawPath(curvePath(t.curve, i0, i1), th.gr, style = Stroke(2.5f * density))
                    }
                    val x0 = minOf(0f, x)
                    val i = ((x0 + SCALE) / SCALE * (n - 1)).toInt().coerceIn(0, n - 1)
                    drawCircle(th.text, 3 * density, Offset(xOf(x0, w), yOf(t.curve[i], h)))
                }
            }
            handle(Offset(xOf(xc, w), yOf(ceil, h)), th, null, null)
            title(text, "TRANSFER", th)
        }
    }
}

/** Блок стержней справа в окне (как индикация при MST, шкала дисплея — 48 dB, 0 dB у отступа сверху). */
@Composable
private fun DynBlock(s: ScreenState, m: Int, modifier: Modifier) {
    val th = LocalTheme.current
    Box(modifier.clip(RoundedCornerShape(Layout.radius.dp)).background(th.panel).padding(start = 8.dp, end = 8.dp, top = TOP_INSET.dp)) {
        DynBars(s.masterBars, m - 1, SCALE, Modifier.fillMaxSize(), scale = SCALE, alignTop = true)
    }
}

/** TRANSFER сатуратора: оси −1…+1, кривая, точка текущего уровня; палец по вертикали — DRIVE (240 точек на диапазон). */
@Composable
private fun SatTransfer(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    var start = 0.0
    Box(modifier.clip(RoundedCornerShape(topStart = Layout.radius.dp, bottomStart = Layout.radius.dp)).background(th.panel)
        .background(th.bg.copy(alpha = 0.5f))) {
        Canvas(Modifier.fillMaxSize().pointerInput(Unit) {
            dragGesture({ start = AppNative.masterParam("sat.drive", 1, 0.0); true }, { d, _ ->
                AppNative.masterParam("sat.drive", 4, start - d.y / density / 240.0)
                s.refresh()
            }) { }
        }) {
            val v = s.masterDisp
            val n = v.getOrElse(0) { 0f }.toInt()
            val tn = v.getOrElse(1 + 2 * n) { 0f }.toInt()
            val curve = slice(v, 2 + 2 * n, tn)
            val probe = v.getOrElse(2 + 2 * n + tn) { 0f }
            val w = size.width
            val h = size.height
            for (k in 1 until 4) {
                val f = k / 4f
                drawRect(th.line, Offset(Math.round(f * w).toFloat(), 0f), Size(density, h))
                drawRect(th.line, Offset(0f, Math.round(f * h).toFloat()), Size(w, density))
            }
            drawLine(th.dim, Offset(0f, h), Offset(w, 0f), density)
            fun pt(x: Float, y: Float) = Offset((x + 1) / 2 * w, ((1 - y) / 2 * h).coerceIn(0f, h))
            fun at(x: Float): Float {
                if (curve.size < 2) return x
                val i = ((x + 1) / 2 * (curve.size - 1)).coerceIn(0f, (curve.size - 1).toFloat())
                val i0 = i.toInt()
                val i1 = minOf(curve.size - 1, i0 + 1)
                return curve[i0] + (curve[i1] - curve[i0]) * (i - i0)
            }
            val bypass = s.masterWin[2] != 0
            if (curve.size > 1) {
                val p = Path()
                for (i in curve.indices) {
                    val q = pt(-1 + 2f * i / (curve.size - 1), curve[i])
                    if (i == 0) p.moveTo(q.x, q.y) else p.lineTo(q.x, q.y)
                }
                drawPath(p, if (bypass) th.dim else th.text, style = Stroke(1.5f * density))
            }
            if (!bypass && abs(probe) >= 1e-4f) {
                val x = probe.coerceIn(-1f, 1f)
                drawCircle(th.accent, 3 * density, pt(x, at(x)))
            }
            val q = pt(0.5f, at(0.5f))
            handle(q, th, null, null)
            val drive = s.masterWinTexts.let { t -> (0 until t.size / 3).firstOrNull { t[3 * it] == "sat.drive" }?.let { t[3 * it + 2] } ?: "" }
            val lt = text.measure("DRIVE $drive", TextStyle(color = th.text, fontSize = (10 * canvasTextScale).sp()))
            drawText(lt, topLeft = Offset(q.x - lt.size.width / 2, q.y + 13 * density + 4 * density))
            title(text, "TRANSFER", th)
        }
        Box(Modifier.width(1.dp).fillMaxHeight().background(th.line).align(androidx.compose.ui.Alignment.CenterEnd))
    }
}

/** SPECTRUM сатуратора: 10 Гц…20 кГц, 0…−96 dB; вход — серая заливка, выход — белый контур; линия FOCUS тянется
 *  по горизонтали; двойной тап мимо линии — FREEZE. */
@Composable
private fun SatSpectrum(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val text = rememberTextMeasurer()
    val dt = DoubleTap()
    val octaves = log2(20000.0 / 10.0)
    var startX = 0f
    Box(modifier.clip(RoundedCornerShape(topEnd = Layout.radius.dp, bottomEnd = Layout.radius.dp)).background(th.panel)) {
        Canvas(Modifier.fillMaxSize().pointerInput(Unit) {
            fun x(f: Double) = (size.width * log2(f / 10.0) / octaves).toFloat()
            fun freq(x: Float) = 10.0 * 2.0.pow(x / maxOf(1, size.width) * octaves)
            dragGesture({ p ->
                startX = x(AppNative.masterParam("sat.focus", 2, 0.0))
                abs(p.x - startX) <= LINE_HIT * density
            }, { d, _ ->
                AppNative.masterParam("sat.focus", 3, freq(startX + d.x))
                s.refresh()
            }) { if (dt.tap()) { AppNative.masterWinAct(MasterWinAct.FREEZE, 0, 0, ""); s.refresh() } }
        }) {
            val v = s.masterDisp
            val n = v.getOrElse(0) { 0f }.toInt()
            val dIn = slice(v, 1, n)
            val dOut = slice(v, 1 + n, n)
            val w = size.width
            val h = size.height
            fun y(db: Float) = (-db / 96f * h).coerceIn(0f, h)
            fun x(f: Double) = (w * log2(f / 10.0) / octaves).toFloat()
            var db = 0
            val dim = TextStyle(color = th.dim, fontSize = (10 * canvasTextScale).sp())
            while (db > -96) {
                val yy = Math.round(y(db.toFloat())).toFloat()
                drawRect(th.line, Offset(0f, yy), Size(w, density))
                drawText(text.measure(if (db == 0) "0" else "$db", dim), topLeft = Offset(4 * density, yy + 2 * density))
                db -= 12
            }
            for ((f, l) in listOf(100.0 to "100", 1000.0 to "1k", 10000.0 to "10k")) {
                val xx = Math.round(x(f)).toFloat()
                drawRect(th.line, Offset(xx, 0f), Size(density, h))
                val t = text.measure(l, dim)
                drawText(t, topLeft = Offset(xx + 3 * density, h - 4 * density - t.size.height))
            }
            if (n > 1) {
                val dx = w / (n - 1)
                val fill = Path().apply {
                    moveTo(0f, h)
                    for (i in 0 until n) lineTo(i * dx, y(dIn[i]))
                    lineTo((n - 1) * dx, h)
                    close()
                }
                drawPath(fill, th.dim.copy(alpha = 0.35f))
                val line = Path().apply { for (i in 0 until n) if (i == 0) moveTo(0f, y(dOut[0])) else lineTo(i * dx, y(dOut[i])) }
                drawPath(line, th.text, style = Stroke(1.5f * density))
            }
            val focus = AppNative.masterParam("sat.focus", 2, 0.0)
            val fx = maxOf(density, Math.round(x(focus)).toFloat())
            drawRect(th.text, Offset(fx - density, 0f), Size(2 * density, h))
            val ft = s.masterWinTexts.let { t -> (0 until t.size / 3).firstOrNull { t[3 * it] == "sat.focus" }?.let { t[3 * it + 2] } ?: "" }
            drawText(text.measure("FOCUS $ft", TextStyle(color = th.text, fontSize = (11 * canvasTextScale).sp())),
                topLeft = Offset(fx + 6 * density, 24 * density))
            title(text, "SPECTRUM", th)
            if (s.masterWin[6] != 0) label(text, "FREEZE", th.accent, 30f, 6f, true)
        }
    }
}
