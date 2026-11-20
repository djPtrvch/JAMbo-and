package app.jambo.android.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.jambo.android.LibAct

/** Строка браузера из libRows: id, вид (f — папка, s — сэмпл, p — пресет), имя, справа, звук дорожки, отмечен. */
class LibItem(val id: String, val kind: String, val name: String, val meta: String, val current: Boolean, val picked: Boolean) {
    val folder get() = kind == "f"
    val playable get() = kind == "s" || kind == "p"
    val system get() = id == "recordings"
}

/**
 * Строка браузера (LibraryRow.swift ветки swift JAMbo): слева ▶ у сэмпла и пресета (предпрослушка, звук не загружает),
 * значок и имя, справа — число в папке или движок; тап — войти, загрузить или отметить; удержание — выбор нескольких;
 * свайп влево — EDIT и корзина (у непустой папки — второй тап по «DELETE?»). Открыта одна строка (open — у списка).
 */
@Composable
fun LibraryRow(s: ScreenState, n: LibItem, open: Boolean, setOpen: (Boolean) -> Unit, rename: () -> Unit) {
    val th = LocalTheme.current
    val button = 52f
    var confirm by remember(n.id) { mutableStateOf(false) }
    val needConfirm = n.folder && n.meta != "0"
    val reveal = if (n.system) 0f else 2 * button + 4 + (if (confirm) 40f else 0f)
    var drag by remember { mutableFloatStateOf(0f) }
    var dragging by remember { mutableStateOf(false) }
    val off by animateFloatAsState(if (dragging) drag else if (open) -reveal else 0f, label = "libswipe")
    val shown = if (dragging) drag else off
    Box(Modifier.fillMaxWidth().height(44.dp)) {
        if (shown < -1) Row(Modifier.align(Alignment.CenterEnd).fillMaxHeight(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            Box(Modifier.width(button.dp).fillMaxHeight().tile(Color(0xFF8E8E93), 8f).pointerTap { setOpen(false); rename() },
                contentAlignment = Alignment.Center) { Text("EDIT", color = Color.White, fontSize = 11.tsp(), fontWeight = FontWeight.Bold) }
            Box(Modifier.width((button + if (confirm) 40f else 0f).dp).fillMaxHeight().tile(th.rec, 8f).pointerTap {
                if (needConfirm && !confirm) confirm = true
                else { setOpen(false); confirm = false; s.libAct(LibAct.DELETE, n.id) }
            }, contentAlignment = Alignment.Center) {
                Text(if (confirm) "DELETE?" else "🗑", color = Color.White, fontSize = if (confirm) 12.tsp() else 16.tsp(),
                    fontWeight = FontWeight.Bold)
            }
        }
        val bg = if (n.current) th.accent else th.panel
        val fg = if (n.current) th.onAccent else th.text
        Row(
            Modifier.fillMaxHeight().fillMaxWidth().padding(end = (-shown).coerceAtLeast(0f).dp).tile(bg, 8f)
                .then(if (n.picked) Modifier.border(1.dp, th.accent, RoundedCornerShape(8.dp)) else Modifier),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (n.playable) Box(Modifier.width(40.dp).fillMaxHeight().pointerTap { s.libAct(LibAct.PREVIEW, n.id) },
                contentAlignment = Alignment.Center) { Text("▶", color = fg, fontSize = 14.isp()) }
            else Spacer(Modifier.width(10.dp))
            Row(
                Modifier.weight(1f).fillMaxHeight()
                    .pointerInput(open, reveal) {
                        if (reveal > 0) detectHorizontalDragGestures(
                            onDragStart = { dragging = true; drag = if (open) -reveal else 0f },
                            onDragEnd = { dragging = false; setOpen(drag < -reveal / 2); if (drag > -reveal / 2) confirm = false },
                            onDragCancel = { dragging = false },
                        ) { _, dx -> drag = (drag + dx / density).coerceIn(-reveal, 0f) }
                    }
                    .pointerInput(n.id, open) {
                        detectTapGestures(
                            onLongPress = { if (!open) s.libAct(LibAct.HOLD, n.id) },
                            onTap = { if (open) { setOpen(false); confirm = false } else s.libAct(LibAct.TAP, n.id) },
                        )
                    }
                    .padding(end = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                RowIcon(n, fg, th.rec)
                Text(n.name, color = fg, fontSize = 13.tsp(), fontWeight = FontWeight.Bold, maxLines = 1,
                    overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                if (shown > -1 && n.meta.isNotEmpty())
                    Text(n.meta, color = fg.copy(alpha = 0.65f), fontSize = 12.tsp(), maxLines = 1)
                if (n.picked) Text("✓", color = fg, fontSize = 14.isp(), fontWeight = FontWeight.Bold)
            }
        }
    }
}

/** Значок строки: RECORDINGS — красная точка, папка — контур папки, сэмпл ∿, пресет ◆. */
@Composable
private fun RowIcon(n: LibItem, fg: Color, rec: Color) {
    Box(Modifier.width(22.dp), contentAlignment = Alignment.Center) {
        when {
            n.system -> Canvas(Modifier.size(7.dp)) { drawCircle(rec) }
            n.folder -> Canvas(Modifier.size((16 * LocalIconScale.current).dp, (12 * LocalIconScale.current).dp)) {
                val w = size.width
                val h = size.height
                val p = Path().apply {
                    moveTo(0f, h); lineTo(0f, 0f); lineTo(w * 0.4f, 0f); lineTo(w * 0.5f, h * 0.18f)
                    lineTo(w, h * 0.18f); lineTo(w, h); close()
                }
                drawPath(p, fg, style = Stroke(1.6f * density))
                drawLine(fg, Offset(0f, h * 0.36f), Offset(w, h * 0.36f), 1.2f * density)
            }
            else -> Text(if (n.kind == "s") "∿" else "◆", color = fg, fontSize = 15.isp(),
                fontWeight = FontWeight.Bold)
        }
    }
}
