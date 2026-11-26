package app.jambo.android.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import app.jambo.android.MidiTarget

/** Метки LEARN поверх цели: рамка выделения, заливка и источник назначения. */
@Composable
fun Modifier.midiLearnMark(s: ScreenState, target: Int, radius: Float = Layout.radius): Modifier {
    val controls = s.midi.controls
    if (!controls.learning) return this
    val selected = controls.learnTarget == target
    val binding = controls.bindingForTarget(target)
    if (!selected && binding == null) return this

    val theme = LocalTheme.current
    val text = rememberTextMeasurer()
    val style = TextStyle(color = theme.text, fontSize = 9.tsp(), fontWeight = FontWeight.SemiBold)
    return drawWithCache {
        val inset = 4.dp.toPx()
        val corner = CornerRadius(radius.dp.toPx())
        val stroke = 2.dp.toPx()
        val badge = binding?.let {
            val prefix = when {
                target in MidiTarget.PRESS until MidiTarget.PLAY -> "P"
                it.status and 0xF0 == 0xB0 -> "CC"
                else -> "N"
            }
            text.measure("$prefix ${it.number}", style, maxLines = 1)
        }
        onDrawWithContent {
            drawContent()
            if (binding != null) drawRoundRect(theme.accent.copy(alpha = 0.22f), cornerRadius = corner)
            if (badge != null && badge.size.width + 2 * inset <= size.width && badge.size.height + inset <= size.height) {
                drawText(badge, topLeft = Offset(size.width - inset - badge.size.width, inset))
            }
            if (selected && size.width > stroke && size.height > stroke) {
                drawRoundRect(theme.accent, topLeft = Offset(stroke / 2, stroke / 2),
                    size = Size(size.width - stroke, size.height - stroke),
                    cornerRadius = CornerRadius((corner.x - stroke / 2).coerceAtLeast(0f)),
                    style = Stroke(stroke))
            }
        }
    }
}
