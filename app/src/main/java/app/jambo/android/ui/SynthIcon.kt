package app.jambo.android.ui

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.aspectRatio
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
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import app.jambo.android.AppNative
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min

/**
 * Иконка движка (SynthWindow.swift ветки swift JAMbo): элементы EngineIcons (C++, поле 64×64) двумя цветами темы
 * (icon — звук, icon2 — серый). Ручки сглажены (~90 мс): кадры идут, пока сглаживание не дошло до значений ручек;
 * другая дорожка — сразу её значения.
 */
@Composable
fun SynthIcon(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val target = s.iconValues
    val engine = target[0].toInt()
    val track = s.tracks[1]
    var shown by remember(track, engine) { mutableStateOf(target) }
    LaunchedEffect(target, track, engine) {
        var last = 0L
        while (!shown.contentEquals(target)) {
            withFrameNanos { now ->
                val dt = if (last == 0L) 0.0 else min(0.05, max(0.0, (now - last) / 1e9))
                last = now
                shown = iconStep(shown, target, dt)
            }
        }
    }
    val v = shown
    val els = remember(v) {
        AppNative.icon(engine, v[1], v[2], v[3], v[4], v[5], v[6].toInt(), v[7] != 0f)
    }
    Canvas(modifier.aspectRatio(1f)) { drawIcon(els, th.icon, th.icon2) }
}

/** Шаг сглаживания: 1 − exp(−dt / 0,09); am, ao — сразу; почти дошло — ровно target. */
private fun iconStep(s: FloatArray, t: FloatArray, dt: Double): FloatArray {
    val a = (1 - exp(-dt / 0.09)).toFloat()
    val o = s.copyOf()
    var far = 0f
    for (i in 1..5) {
        o[i] += (t[i] - o[i]) * a
        far = max(far, abs(t[i] - o[i]))
    }
    o[0] = t[0]; o[6] = t[6]; o[7] = t[7]
    return if (far < 0.0005f) t else o
}

private fun DrawScope.drawIcon(e: FloatArray, ink: Color, ink2: Color) {
    val side = min(size.width, size.height)
    val k = side / 64f
    translate((size.width - side) / 2, (size.height - side) / 2) {
        var i = 0
        while (i < e.size) {
            val kind = e[i].toInt()
            val color = (if (e[i + 1] != 0f) ink2 else ink).copy(alpha = e[i + 2])
            val fill = e[i + 3] != 0f
            val dash = e[i + 4] != 0f
            i += 5
            when (kind) {
                0 -> {
                    val contour = e[i] != 0f
                    val n = e[i + 1].toInt()
                    val r = FloatArray(n * 4) { e[i + 2 + it] }
                    i += 2 + n * 4
                    drawPath(if (contour) contourPath(r, k) else rectsPath(r, k), color)
                }
                1 -> {
                    val n = e[i].toInt()
                    val x0 = e[i + 1]; val x1 = e[i + 2]
                    val top = FloatArray(n) { e[i + 3 + it] }
                    val bot = FloatArray(n) { e[i + 3 + n + it] }
                    i += 3 + 2 * n
                    drawPath(wavePath(top, bot, x0, x1, k), color)
                }
                2 -> {
                    val n = e[i].toInt()
                    val p = cmdPath(e, i + 1, n, k)
                    i += 1 + 3 * n
                    if (fill) drawPath(p, color)
                    else drawPath(p, color, style = Stroke(1.4f * k, cap = StrokeCap.Round, join = StrokeJoin.Round,
                        pathEffect = if (dash) PathEffect.dashPathEffect(floatArrayOf(2 * k, 2.4f * k)) else null))
                }
                3 -> {
                    val x = e[i]; val y = e[i + 1]; val sz = e[i + 2]; val len = e[i + 3].toInt()
                    val text = String(IntArray(len) { e[i + 4 + it].toInt() }, 0, len)
                    i += 4 + len
                    drawIntoCanvas { c ->
                        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                            this.color = color.toArgb()
                            textSize = sz * k
                            textAlign = Paint.Align.CENTER
                            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
                        }
                        // Подпись по центру, y — базовая линия (как text-anchor middle в SVG).
                        c.nativeCanvas.drawText(text, x * k, y * k, paint)
                    }
                }
                4 -> {
                    val c = Offset(e[i] * k, e[i + 1] * k)
                    val r = e[i + 2] * k
                    i += 3
                    if (fill) drawCircle(color, r, c) else drawCircle(color, r, c, style = Stroke(1.4f * k))
                }
                else -> return@translate
            }
        }
    }
}

/** Отдельные прямоугольники одним путём, со скруглёнными концами: перекрытия не темнеют. */
private fun rectsPath(r: FloatArray, k: Float): Path {
    val p = Path()
    var j = 0
    while (j < r.size) {
        val x = r[j] * k; val y = r[j + 1] * k; val w = r[j + 2] * k; val h = r[j + 3] * k
        val rad = min(w, h) / 2
        p.addRoundRect(RoundRect(x, y, x + w, y + h, CornerRadius(rad, rad)))
        j += 4
    }
    return p
}

/** Волна — сплошная фигура: верхний край слева направо, нижний обратно, сглажено. */
private fun wavePath(top: FloatArray, bot: FloatArray, x0: Float, x1: Float, k: Float): Path {
    val n = min(top.size, bot.size)
    val p = Path()
    if (n < 2) return p
    val dx = (x1 - x0) * k / (n - 1)
    val tx = FloatArray(n) { x0 * k + it * dx }
    val t = List(n) { Offset(tx[it], top[it] * k) }
    val b = List(n) { Offset(tx[n - 1 - it], bot[n - 1 - it] * k) }
    p.moveTo(t[0].x, t[0].y)
    addSmooth(p, t)
    p.lineTo(b[0].x, b[0].y)
    addSmooth(p, b)
    p.close()
    return p
}

/** Огибающая из столбиков — сплошная фигура: верх по центрам столбиков, низ обратно; разрывы — отдельные фигуры. */
private fun contourPath(r: FloatArray, k: Float): Path {
    val p = Path()
    val run = ArrayList<FloatArray>()
    fun flush() {
        if (run.isEmpty()) return
        val a = run.first(); val b = run.last()
        if (run.size == 1) {
            p.addRect(androidx.compose.ui.geometry.Rect(a[0] * k, a[1] * k, (a[0] + a[2]) * k, (a[1] + a[3]) * k))
            run.clear(); return
        }
        val top = ArrayList<Offset>()
        val bot = ArrayList<Offset>()
        top.add(Offset(a[0] * k, a[1] * k))
        bot.add(Offset((b[0] + b[2]) * k, (b[1] + b[3]) * k))
        for (q in run) top.add(Offset((q[0] + q[2] / 2) * k, q[1] * k))
        top.add(Offset((b[0] + b[2]) * k, b[1] * k))
        for (q in run.asReversed()) bot.add(Offset((q[0] + q[2] / 2) * k, (q[1] + q[3]) * k))
        bot.add(Offset(a[0] * k, (a[1] + a[3]) * k))
        p.moveTo(top[0].x, top[0].y)
        addSmooth(p, top)
        p.lineTo(bot[0].x, bot[0].y)
        addSmooth(p, bot)
        p.close()
        run.clear()
    }
    var j = 0
    while (j < r.size) {
        val q = floatArrayOf(r[j], r[j + 1], r[j + 2], r[j + 3])
        // Соседний столбик — вплотную справа (у столбиков lane — перекрытие 0,12).
        val prev = run.lastOrNull()
        if (prev != null && abs(q[0] - (prev[0] + prev[2])) > 0.3f) flush()
        run.add(q)
        j += 4
    }
    flush()
    return p
}

/** Плавно через точки (путь уже стоит в pts[0]): квадратичные кривые к серединам отрезков. */
private fun addSmooth(p: Path, pts: List<Offset>) {
    val n = pts.size
    if (n < 2) return
    for (i in 1 until n - 1) {
        val a = pts[i]; val b = pts[i + 1]
        p.quadraticTo(a.x, a.y, (a.x + b.x) / 2, (a.y + b.y) / 2)
    }
    p.lineTo(pts[n - 1].x, pts[n - 1].y)
}

/** Команды контура: 0 move, 1 line, 2 hTo, 3 vTo, 4 hBy, 5 vBy, 6 close (IconCmd в C++). */
private fun cmdPath(e: FloatArray, at: Int, n: Int, k: Float): Path {
    val p = Path()
    var x = 0f; var y = 0f
    for (c in 0 until n) {
        val o = at + 3 * c
        val a = e[o + 1] * k; val b = e[o + 2] * k
        when (e[o].toInt()) {
            0 -> { x = a; y = b; p.moveTo(x, y) }
            1 -> { x = a; y = b; p.lineTo(x, y) }
            2 -> { x = a; p.lineTo(x, y) }
            3 -> { y = a; p.lineTo(x, y) }
            4 -> { x += a; p.lineTo(x, y) }
            5 -> { y += a; p.lineTo(x, y) }
            else -> p.close()
        }
    }
    return p
}
