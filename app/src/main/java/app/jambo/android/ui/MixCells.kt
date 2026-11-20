package app.jambo.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import kotlin.math.log10

/** Раскладка ячейки MIX (MixLayout ветки swift JAMbo): слева метр, ручка, справа столбик M, S, ●. */
const val MIX_METER = 21f
const val MIX_BUTTON = 26f
private const val MIX_LABEL = 17f

/** Кнопки M, S, ● в точках ячейки. */
fun mixButtons(cell: Size): List<Rect> {
    val knobH = cell.height - MIX_LABEL
    val bh = ((knobH - 8) / 3).coerceIn(14f, 26f)
    val total = 3 * bh + 8
    val y0 = knobH / 2 - total / 2 + 2
    val x = cell.width - MIX_BUTTON - 2
    return (0 until 3).map { Rect(Offset(x, y0 + it * (bh + 4)), Size(MIX_BUTTON, bh)) }
}

/** Ячейка MIX поверх ручки: метр дорожки (после громкости, стерео — две полосы, Sidechain — оранжевым сверху) и
 *  кнопки M (красная), S (жёлтая), ● (запись стема) — как REC: окантовка цветом, включена — горит. */
@Composable
fun MixCellParts(s: ScreenState, slot: Int) {
    val th = LocalTheme.current
    val info = s.mixInfo
    val o = 5 + 4 * slot
    val mute = info.getOrElse(o) { 0 } != 0
    val solo = info.getOrElse(o + 1) { 0 } != 0
    val armed = info.getOrElse(o + 2) { 0 } != 0
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val cell = Size(maxWidth.value, maxHeight.value)
        val h = (cell.height - MIX_LABEL) * 0.8f
        Canvas(Modifier.offset(2.dp, ((cell.height - MIX_LABEL - h) / 2).dp).size(MIX_METER.dp, h.dp)) { meter(s.mixer, slot, th) }
        val r = mixButtons(cell)
        small("M", mute, th.rec, r[0])
        small("S", solo, th.accent, r[1])
        Box(Modifier.offset(r[2].left.dp, r[2].top.dp).size(r[2].width.dp, r[2].height.dp)
            .background(if (armed) th.rec else Color.Transparent, RoundedCornerShape(5.dp))
            .border(1.dp, th.rec, RoundedCornerShape(5.dp)), contentAlignment = Alignment.Center) {
            Box(Modifier.size(8.dp).background(if (armed) Color.White else th.rec, CircleShape))
        }
    }
}

@Composable
private fun small(title: String, on: Boolean, color: Color, r: Rect) {
    val th = LocalTheme.current
    Box(Modifier.offset(r.left.dp, r.top.dp).size(r.width.dp, r.height.dp)
        .background(if (on) color else Color.Transparent, RoundedCornerShape(5.dp)).border(1.dp, color, RoundedCornerShape(5.dp)),
        contentAlignment = Alignment.Center) {
        Text(title, color = if (on) th.onAccent else color, fontSize = 11.tsp(), fontWeight = FontWeight.Bold)
    }
}

/** Метр ячейки: данные mixerData (стерео, заглушена, L, холд, R, холд, Sidechain dB…), полосы у правого края. */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.meter(v: FloatArray, t: Int, th: Theme) {
    val o = 1 + 10 * t
    if (o + 10 > v.size) return
    val stereo = v[o] != 0f
    val muted = v[o + 1] != 0f
    val bars = if (stereo) listOf(v[o + 2] to v[o + 3], v[o + 4] to v[o + 5]) else listOf(v[o + 2] to v[o + 3])
    val duck = v[o + 6]
    val gr = if (duck <= -0.1f) -duck / 24 else 0f
    val w = 9 * density
    val span = bars.size * w + (bars.size - 1) * 3 * density
    val h = size.height
    fun frac(p: Float) = if (p <= 0) 0f else ((20 * log10(p) + 60) / 60).coerceIn(0f, 1f)
    for ((k, b) in bars.withIndex()) {
        val x = size.width - span + k * (w + 3 * density)
        val shape = Path().apply { addRoundRect(RoundRect(x, 0f, x + w, h, CornerRadius(3 * density))) }
        drawPath(shape, th.meterTrack)
        clipPath(shape) {
            val f = frac(b.first)
            val fill = if (muted) th.muteGrey else if (b.first >= 0.999f) th.rec else th.accent
            if (f > 0) drawRect(fill, Offset(x, h - f * h), Size(w, f * h))
            val hf = frac(b.second)
            if (hf > 0.01f) drawRect(th.text, Offset(x, maxOf(0f, h - hf * h - density)), Size(w, 2 * density))
            if (gr > 0) drawRect(th.gr, Offset(x, 0f), Size(w, minOf(1f, gr) * h))
        }
    }
}

/** Меню дорожки у ручки громкости (MIX): COPY, PASTE, DELETE TRACK, RENAME; у серой — ADD TRACK. Тап мимо — закрыть. */
@Composable
fun TrackMenuOverlay(s: ScreenState, grid: Rect, onRename: (Int) -> Unit) {
    val th = LocalTheme.current
    val d = androidx.compose.ui.platform.LocalDensity.current.density
    val info = s.mixInfo
    val slot = info.getOrElse(0) { -1 }
    if (slot < 0 || grid.width <= 0) return
    val add = info.getOrElse(1) { 0 } != 0
    Box(Modifier.fillMaxSize().pointerTap { AppNative.mixAct(5, 0, 0, ""); s.refresh() })
    val cw = grid.width / d / 4
    val ch = grid.height / d / 4
    val cx = grid.left / d + cw * (slot % 4 + 0.5f)
    val cy = grid.top / d + ch * (slot / 4 + 0.5f)
    val rows = if (add) 1 else 4
    val mh = rows * 46f + 10
    val x = maxOf(grid.left / d, minOf(cx - 98, grid.right / d - 196))
    val below = cy + ch / 2
    val y = if (below + mh < grid.bottom / d) below else maxOf(grid.top / d, cy - ch / 2 - mh)
    val shape = RoundedCornerShape(Layout.radius.dp)
    Column(Modifier.offset(x.dp, y.dp).width(180.dp).shadow(12.dp, shape).background(th.bg, shape).border(1.dp, th.line, shape)
        .padding(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        fun act(code: Int) { AppNative.mixAct(code, slot, 0, ""); s.refresh() }
        if (add) MenuItem("ADD TRACK") { act(6) }
        else {
            MenuItem("COPY") { act(7) }
            MenuItem("PASTE", enabled = info.getOrElse(2) { 0 } != 0) { act(8) }
            MenuItem("DELETE TRACK", enabled = info.getOrElse(3) { 0 } != 0, red = true) { act(9) }
            MenuItem("RENAME") { AppNative.mixAct(5, 0, 0, ""); s.refresh(); onRename(slot) }
        }
    }
}
