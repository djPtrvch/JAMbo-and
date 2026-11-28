package app.jambo.android.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.floor

internal data class ArrangeReference(val name: String, val offset: Double, val bpm: Double, val peaks: FloatArray)

@Composable
internal fun arrangeReference(s: ScreenState, visible: Boolean): ArrangeReference? {
    if (!visible) return null
    val file = remember(s.revision) { AppNative.arrangeRefName() }
    val key = file.getOrNull(0) ?: return null
    val info = remember(s.revision) { AppNative.arrangeRefInfo() }
    val peaks by produceState(FloatArray(0), key) {
        value = withContext(Dispatchers.Default) { AppNative.arrangeRefPeaks(key) }
    }
    return ArrangeReference(file.getOrElse(1) { "" }, info[0], info[1], peaks)
}

internal fun DrawScope.drawArrangeReference(
    track: ArrangeReference, header: Float, top: Float, row: Float, stepWidth: Float, scroll: Float,
    stepSeconds: Double, theme: Theme,
) {
    drawRect(theme.trackBg, Offset(0f, top), Size(size.width, row))
    val hopSeconds = 256 / 48_000.0
    val column = 2.dp.toPx().coerceAtLeast(1f)
    val mid = top + row / 2
    val halfHeight = (row / 2 - 3.dp.toPx()).coerceAtLeast(1f)
    var x = header
    while (x < size.width) {
        fun index(px: Float) = (((px - header + scroll) / stepWidth * stepSeconds - track.offset) / hopSeconds)
        val a = floor(index(x)).toInt()
        val b = floor(index(x + column)).toInt()
        if (b >= 0 && a < track.peaks.size) {
            var peak = 0f
            for (i in maxOf(0, a)..minOf(b, track.peaks.lastIndex)) peak = maxOf(peak, track.peaks[i])
            val h = (peak.coerceAtMost(1f) * halfHeight).coerceAtLeast(0.5f)
            drawRect(theme.text.copy(alpha = 0.6f), Offset(x, mid - h), Size((column - 0.5f).coerceAtLeast(0.5f), h * 2))
        }
        x += column
    }
}
