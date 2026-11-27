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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import app.jambo.android.MidiTarget
import kotlin.math.sin

@Composable
internal fun ArrangeToolbar(
    s: ScreenState, d: ArrangeData, gridOpen: Boolean, knobs: Boolean,
    grid: () -> Unit, split: () -> Unit, toggleKnobs: () -> Unit,
) {
    val spacing = Arrangement.spacedBy(Layout.gap.dp)
    Row(Modifier.fillMaxWidth().height(Layout.topHeight.dp), horizontalArrangement = spacing) {
        Row(Modifier.width(column()).fillMaxHeight(), horizontalArrangement = spacing) {
            ArrangeButton("Grid", Modifier.weight(1f), gridOpen, action = grid) { ArrangeIcon(0, it) }
            ArrangeButton("Trim", Modifier.weight(1f), action = split) { ArrangeIcon(1, it) }
            ArrangeButton("Return to start", Modifier.weight(1f), action = { s.arrangeAct(ArrangeAct.SEEK, 0) }) { ArrangeIcon(2, it) }
            ArrangeButton("Loop", Modifier.weight(1f), d.loop, enabled = d.loopEnd > d.loopStart,
                action = { s.arrangeAct(ArrangeAct.LOOP) }) { LoopIcon(it) }
        }
        Spacer(Modifier.weight(1f))
        Row(Modifier.width(column()).fillMaxHeight(), horizontalArrangement = spacing) {
            ArrangeButton("Automation", Modifier.weight(1f).midiLearnMark(s, MidiTarget.AUTO), d.auto,
                recording = d.auto && d.playingTimeline && s.top[5] != 0,
                action = { if (!s.learnTarget(MidiTarget.AUTO)) s.arrangeAct(ArrangeAct.AUTO) }) { ArrangeIcon(3, it) }
            ArrangeButton("Knobs", Modifier.weight(1f), knobs, action = toggleKnobs) { ArrangeIcon(4, it) }
            ArrangeButton("Steps", Modifier.weight(1f), !d.hideSteps,
                action = { s.arrangeAct(ArrangeAct.STEPS) }) { ArrangeIcon(5, it) }
            ArrangeButton("Reference track", Modifier.weight(1f), d.ref, action = { s.arrangeAct(ArrangeAct.REF) }) {
                Text("REF", color = it, fontSize = 12.tsp(), fontWeight = FontWeight.SemiBold, maxLines = 1)
            }
        }
        Spacer(Modifier.width(Layout.meterWidth.dp))
    }
}

@Composable
private fun ArrangeButton(
    name: String, modifier: Modifier, selected: Boolean = false, enabled: Boolean = true, recording: Boolean = false,
    action: () -> Unit, content: @Composable (Color) -> Unit,
) {
    val th = LocalTheme.current
    val active = enabled && selected
    val ink = if (!enabled) th.off else if (active) th.bg else th.text
    val ring = if (recording) Modifier.border(3.dp, th.rec, RoundedCornerShape(Layout.radius.dp)) else Modifier
    Box(modifier.fillMaxHeight().tile(if (active) th.text else th.panel2).then(ring)
        .semantics {
            role = Role.Button
            contentDescription = name
            if (enabled) onClick { action(); true } else disabled()
        }
        .then(if (enabled) Modifier.pointerTap(action) else Modifier), contentAlignment = Alignment.Center) {
        content(ink)
    }
}

@Composable
private fun ArrangeIcon(kind: Int, ink: Color) {
    val k = LocalIconScale.current
    Canvas(Modifier.size(((if (kind == 3 || kind == 5) 25 else 22) * k).dp, (22 * k).dp)) {
        val w = size.width
        val h = size.height
        val stroke = (1.8f * k).dp.toPx().coerceAtLeast(1f)
        fun at(x: Float, y: Float) = Offset(x * w, y * h)
        fun line(x: Float, y: Float, a: Float, b: Float) =
            drawLine(ink, at(x, y), at(a, b), stroke, cap = StrokeCap.Round)
        when (kind) {
            0 -> for (n in listOf(0.32f, 0.68f)) {
                line(n, 0.08f, n, 0.92f)
                line(0.08f, n, 0.92f, n)
            }
            1 -> {
                drawCircle(ink, h * 0.13f, at(0.25f, 0.72f), style = Stroke(stroke))
                drawCircle(ink, h * 0.13f, at(0.27f, 0.30f), style = Stroke(stroke))
                line(0.36f, 0.63f, 0.87f, 0.30f)
                line(0.40f, 0.35f, 0.87f, 0.72f)
            }
            2 -> {
                line(0.13f, 0.18f, 0.13f, 0.82f)
                line(0.28f, 0.5f, 0.87f, 0.5f)
                line(0.28f, 0.5f, 0.51f, 0.28f)
                line(0.28f, 0.5f, 0.51f, 0.72f)
            }
            3 -> {
                fun y(x: Float) = 0.5f - sin((x - 0.10f) * 7.85f) * 0.25f
                val path = Path().apply {
                    moveTo(w * 0.1f, h * y(0.1f))
                    for (i in 1..32) {
                        val x = 0.1f + i * 0.8f / 32
                        lineTo(w * x, h * y(x))
                    }
                }
                drawPath(path, ink, style = Stroke(stroke, cap = StrokeCap.Round))
                for (x in listOf(0.1f, 0.3f, 0.7f, 0.9f)) drawCircle(ink, stroke * 0.8f, at(x, y(x)))
            }
            4 -> {
                drawCircle(ink, h * 0.38f, style = Stroke(stroke))
                line(0.5f, 0.5f, 0.68f, 0.32f)
            }
            5 -> for (i in 0..3) {
                val x = 0.08f + i * 0.22f
                val square = Size(w * 0.16f, w * 0.16f)
                drawRoundRect(ink, at(x, 0.5f - square.height / h / 2), square, CornerRadius(stroke / 2),
                    style = if (i % 2 == 0) androidx.compose.ui.graphics.drawscope.Fill else Stroke(stroke / 1.5f))
            }
        }
    }
}

@Composable
internal fun ArrangeMeter(s: ScreenState, modifier: Modifier) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        MeterColumn(s, Modifier.weight(1f).fillMaxWidth())
        val prefs = s.arrangePreferences
        if (!prefs.scaleTracks) {
            ArrangeButton(
                if (prefs.compact) "Compact tracks" else "Expanded tracks",
                Modifier.fillMaxWidth().height(30.dp),
                action = { s.arrangePreferences = prefs.copy(compact = !prefs.compact) },
            ) { ink -> ArrangeTrackSizeIcon(prefs.compact, ink) }
        }
    }
}

@Composable
private fun ArrangeTrackSizeIcon(compact: Boolean, ink: Color) {
    val scale = LocalIconScale.current
    Canvas(Modifier.size((16 * scale).dp)) {
        val w = size.width
        val h = size.height
        val stroke = (1.3f * scale).dp.toPx()
        val top = if (compact) 0.26f else 0.14f
        val bottom = 1 - top
        drawRoundRect(ink, Offset(w * 0.16f, h * top), Size(w * 0.68f, h * (bottom - top)),
            CornerRadius(stroke), style = Stroke(stroke))
        val tip = if (compact) 0.45f else 0.27f
        val tail = if (compact) 0.34f else 0.39f
        for (reverse in listOf(false, true)) {
            val point = Offset(w * 0.5f, h * if (reverse) 1 - tip else tip)
            val y = h * if (reverse) 1 - tail else tail
            drawLine(ink, Offset(w * 0.37f, y), point, stroke, cap = StrokeCap.Round)
            drawLine(ink, Offset(w * 0.63f, y), point, stroke, cap = StrokeCap.Round)
        }
    }
}

@Composable
internal fun ArrangeGridOverlay(s: ScreenState, d: ArrangeData, close: () -> Unit) {
    Box(Modifier.fillMaxSize().pointerTap(close))
    Column(
        Modifier.width(column() + 40.dp).menuBox().pointerTap {}.padding(8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        FieldLabel("GRID")
        MenuChoices(listOf("1 BAR", "1/2", "1/4", "1 STEP").mapIndexed { i, title -> title to (i == d.grid) }) {
            s.arrangeAct(ArrangeAct.GRID, it)
        }
        ToggleRow("SNAP TO GRID", d.snap) { s.arrangeAct(ArrangeAct.SNAP) }
    }
}
