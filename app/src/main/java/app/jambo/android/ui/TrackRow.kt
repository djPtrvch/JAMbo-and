package app.jambo.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act
import kotlin.math.abs

/** Строка дорожек: MIX | 1…N | MST. Тап — выбор; свайп вниз — mute, вверх — solo (срабатывает на ~24 dp). */
@Composable
fun TrackRow(s: ScreenState, step: Float) {
    val tr = s.tracks
    val count = tr[0]
    val channel = tr[2]
    Row(Modifier.height(Layout.trackHeight.dp), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        ChannelCell(s, "MIX", 1, channel == 1, Modifier.width(step.dp))
        for (i in 0 until count) {
            val bits = tr.getOrElse(3 + i) { 0 }
            TrackCell(
                s, i, s.trackNames.getOrElse(i) { "" }, channel == 0 && tr[1] == i,
                mute = bits and 1 != 0, solo = bits and 2 != 0, morph = bits and 4 != 0,
                flash = (s.flashMask shr i) and 1 != 0,
                modifier = Modifier.weight(1f),
            )
        }
        ChannelCell(s, "MST", 2, channel == 2, Modifier.width(step.dp))
    }
}

@Composable
private fun ChannelCell(s: ScreenState, title: String, channel: Int, selected: Boolean, modifier: Modifier) {
    val th = LocalTheme.current
    Box(
        modifier.fillMaxHeight().framed(th.trackBg, if (selected) th.accent else Color.Transparent)
            .pointerInput(channel) { detectTapGestures(onTap = { s.act(Act.SELECT_CHANNEL, channel) }) },
        contentAlignment = Alignment.Center,
    ) {
        Text(title, color = th.text, fontSize = 12.tsp(), fontWeight = if (selected) FontWeight.Black else FontWeight.SemiBold,
            letterSpacing = 0.7.sp)
    }
}

@Composable
private fun TrackCell(
    s: ScreenState, index: Int, name: String, selected: Boolean, mute: Boolean, solo: Boolean, morph: Boolean,
    flash: Boolean,
    modifier: Modifier,
) {
    val th = LocalTheme.current
    val swipePx = with(LocalDensity.current) { 24.dp.toPx() }
    val base = if (flash && !mute) th.flash else th.trackBg
    val bg = when {
        mute -> th.rec.copy(alpha = 0.35f)
        solo -> th.accent.copy(alpha = if (flash) 0.55f else 0.35f)
        else -> base
    }
    val border = if (mute) th.rec else if (selected) th.accent else Color.Transparent
    Box(
        modifier.fillMaxHeight().framed(bg, border).pointerInput(index) {
            awaitEachGesture {
                val down = awaitFirstDown()
                var swiped = false
                while (true) {
                    val ev = awaitPointerEvent()
                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                    val dy = c.position.y - down.position.y
                    if (!swiped && abs(dy) >= swipePx) {
                        swiped = true
                        s.act(Act.SWIPE, index, if (dy > 0) 1 else 0)
                    }
                    if (!c.pressed) break
                }
                if (!swiped) s.act(Act.SELECT_TRACK, index)
            }
        },
    ) {
        Text(
            name, color = th.text, maxLines = 1, modifier = Modifier.align(Alignment.Center),
            fontSize = if (selected) 15.tsp() else 12.tsp(), fontWeight = if (selected) FontWeight.Black else FontWeight.SemiBold,
            letterSpacing = if (selected) 0.sp else 0.7.sp,
        )
        if (mute || solo) {
            Text(
                if (mute) "M" else "S", color = if (mute) th.rec else th.accent, fontSize = 11.tsp(),
                fontWeight = FontWeight.Bold, modifier = Modifier.align(Alignment.TopEnd).padding(top = 3.dp, end = 7.dp),
            )
        }
        // Жёлтая точка: параметры дорожки меняли в режиме MORPH.
        if (morph) {
            Box(Modifier.align(Alignment.TopStart).padding(start = 7.dp, top = 6.dp).size(6.dp)
                .background(th.accent, CircleShape))
        }
        Text(
            "${index + 1}", color = th.dim, fontSize = 10.tsp(), fontWeight = FontWeight.SemiBold,
            modifier = Modifier.align(Alignment.BottomStart).padding(bottom = 3.dp, start = 7.dp),
        )
    }
}

/** Заливка и рамка 1 dp со скруглением (framed в Swift-версии). */
fun Modifier.framed(bg: Color, line: Color, radius: Float = Layout.radius): Modifier {
    val shape = RoundedCornerShape(radius.dp)
    return this.background(bg, shape).border(1.dp, line, shape)
}

/** Заливка со скруглением (tile в Swift-версии). */
fun Modifier.tile(bg: Color, radius: Float = Layout.radius): Modifier = this.background(bg, RoundedCornerShape(radius.dp))
