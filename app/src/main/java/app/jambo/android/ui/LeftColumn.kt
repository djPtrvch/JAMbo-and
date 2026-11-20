package app.jambo.android.ui

import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative.Act

/** Размеры левой и правой колонок (Layout.swift, PagesColumn.swift). */
object Side {
    const val pairHeight = 56f
    const val barGap = 6f
    const val pageRows = 8
    const val pageSpacing = 4f

    /** Строка кнопок над окном: колонка над двумя парами кнопок — 9 равных строк (строка и 8 миниатюр). */
    fun bar(columnHeight: Float): Float {
        val pages = columnHeight - 2 * pairHeight - 2 * Layout.gap
        val rest = pages - barGap - pageSpacing * (pageRows - 1)
        return maxOf(24f, rest / (pageRows + 1))
    }
}

/**
 * Левая колонка (SideColumns.swift ветки swift JAMbo): у дорожки — стрелки папки, окно звука, LFO1–3 и ENV; при MIX —
 * ● MASTER, окно, LEVEL, PAN, MASK, FREQ; при MST — окно, кнопки мастера. Кнопки LFO, MIX и мастера — до своих вех.
 */
@Composable
fun LeftColumn(s: ScreenState, modifier: Modifier) {
    val channel = s.tracks[2]
    BoxWithConstraints(modifier) {
        val bar = Side.bar(maxHeight.value)
        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
            Column(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(Side.barGap.dp)) {
                when (channel) {
                    0 -> FolderArrows(s, Modifier.height(bar.dp))
                    1 -> SideButton("● MASTER", Modifier.fillMaxWidth().height(bar.dp), enabled = false)
                }
                SoundWindow(s, Modifier.weight(1f).fillMaxWidth())
            }
            if (channel == 0) {
                ButtonPair { SourceButton(s, "LFO1", 1, Modifier.weight(1f)); SourceButton(s, "LFO2", 2, Modifier.weight(1f)) }
                ButtonPair { SourceButton(s, "LFO3", 3, Modifier.weight(1f)); SourceButton(s, "ENV", 4, Modifier.weight(1f)) }
            } else if (channel == 2) {
                // По цепочке: SAT снизу слева, над ним COMP, справа от него CLIP, под CLIP — LIM.
                ButtonPair { MasterButton(s, 1, Modifier.weight(1f)); MasterButton(s, 2, Modifier.weight(1f)) }
                ButtonPair { MasterButton(s, 0, Modifier.weight(1f)); MasterButton(s, 3, Modifier.weight(1f)) }
            } else {
                val names = listOf("LEVEL", "PAN", "MASK", "FREQ")
                ButtonPair { SideButton(names[0], Modifier.weight(1f), enabled = false); SideButton(names[1], Modifier.weight(1f), enabled = false) }
                ButtonPair { SideButton(names[2], Modifier.weight(1f), enabled = false); SideButton(names[3], Modifier.weight(1f), enabled = false) }
            }
        }
    }
}

/** LFO1–3, ENV — удержание (назначение на ручки); белый контур — пока палец держит ручку с этим источником. */
@Composable
private fun SourceButton(s: ScreenState, title: String, source: Int, modifier: Modifier) {
    val th = LocalTheme.current
    val held = s.mod[0] and (1 shl source) != 0
    val outlined = s.mod[1] and (1 shl source) != 0
    val line = if (outlined) th.text else Color.Transparent
    HoldButton(title, held, modifier.border(1.dp, line, RoundedCornerShape(Layout.radius.dp))) { on ->
        s.act(Act.SET_SOURCE, source, if (on) 1 else 0, System.nanoTime() / 1e9)
    }
}

/** Кнопка удержания: пока зажата — белая (в светлой теме — чёрная). */
@Composable
fun HoldButton(title: String, held: Boolean, modifier: Modifier, set: (Boolean) -> Unit) {
    val cb by rememberUpdatedState(set)
    SideButton(title, modifier.pointerInput(Unit) {
        awaitEachGesture {
            awaitFirstDown(requireUnconsumed = false).consume()
            cb(true)
            waitForUpOrCancellation()
            cb(false)
        }
    }, lit = held)
}

/** Две кнопки в ряд высотой 56. */
@Composable
fun ButtonPair(content: @Composable RowScope.() -> Unit) {
    Row(Modifier.fillMaxWidth().height(Side.pairHeight.dp), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp), content = content)
}

/** Кнопка боковой колонки: lit — белая (пока зажата), accent — жёлтая. */
@Composable
fun SideButton(
    title: String, modifier: Modifier, enabled: Boolean = true, lit: Boolean = false, accent: Boolean = false,
    icon: Boolean = false, onTap: (() -> Unit)? = null,
) {
    val th = LocalTheme.current
    val fg = if (accent) th.onAccent else if (lit) th.bg else if (enabled) th.text else th.off
    val bg = if (accent) th.accent else if (lit) th.text else th.panel2
    val tap = if (onTap != null && enabled) Modifier.pointerTap(onTap) else Modifier
    Box(modifier.fillMaxHeight().tile(bg).then(tap), contentAlignment = Alignment.Center) {
        Text(title, color = fg, fontSize = if (icon) 13.isp() else 13.tsp(), fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp, maxLines = 1)
    }
}

/** Тап (отпустили на месте); обработчик — всегда последний переданный. */
@Composable
fun Modifier.pointerTap(onTap: () -> Unit): Modifier {
    val cb by rememberUpdatedState(onTap)
    return this.pointerInput(Unit) { detectTapGestures(onTap = { cb() }) }
}

/** Стрелки ◀ ▶ над окном звука: соседний звук папки дорожки (с зажатым шагом — звук шага); между ними n / всего. */
@Composable
private fun FolderArrows(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val total = s.sound[1]
    Row(modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp), verticalAlignment = Alignment.CenterVertically) {
        SideButton("◀", Modifier.width(56.dp), enabled = total > 0, icon = true) { s.act(Act.FOLDER_ARROW, -1) }
        Text(s.soundTexts[3], color = th.dim, fontSize = 11.tsp(), modifier = Modifier.weight(1f),
            textAlign = TextAlign.Center)
        SideButton("▶", Modifier.width(56.dp), enabled = total > 0, icon = true) { s.act(Act.FOLDER_ARROW, 1) }
    }
}

/**
 * Окно звука (SampleWindow.swift, SynthWindow.swift): у синта — имя движка (жёлтым; у WTABLE и GRANULAR — после тире
 * таблица или сэмпл, у пресета — его имя), живая иконка и строки ручек 1.1–1.4 «подпись · значение»; у сэмпла — имя
 * и нота, форма волны (SampleWave). Тап — меню звука (с зажатым шагом — звук этих шагов). Start/End, MIX и MST — позже.
 */
@Composable
private fun SoundWindow(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val channel = s.tracks[2]
    val tap = if (channel == 0) Modifier.pointerTap { s.act(Act.OPEN_SOUND_MENU) } else Modifier
    Box(modifier.tile(th.panel).then(tap)) {
        if (channel == 2) {
            MasterIndicator(s, Modifier.fillMaxSize())
            return@Box
        }
        if (channel != 0) return@Box
        // Пока держат MOD — миниатюры LFO1–3 и ENV.
        if (s.side[6] != 0) {
            ModPreview(Modifier.fillMaxSize())
            return@Box
        }
        val t = s.soundTexts
        val rows = t.drop(4)
        Column(Modifier.fillMaxSize().padding(8.dp)) {
            SoundTitle(t[0], t[1], t[2], Modifier.padding(top = 6.dp))
            val synth = s.sound[0] >= 0
            Column(
                Modifier.weight(1f).fillMaxWidth().padding(horizontal = 6.dp),
                verticalArrangement = if (synth) Arrangement.Center else Arrangement.Top,
            ) {
                if (synth) {
                    // Иконка — до 144 точек, ужимается по высоте окна (сдвиг на 3 вниз — по проверке на iPad).
                    Box(Modifier.fillMaxWidth().weight(1f, fill = false).heightIn(max = 144.dp), contentAlignment = Alignment.Center) {
                        SynthIcon(s, Modifier.offset(y = 3.dp))
                    }
                    Spacer(Modifier.height(8.dp))
                } else {
                    SampleWave(s, Modifier.weight(1f).fillMaxWidth().padding(vertical = 8.dp))
                }
                Column(Modifier.padding(top = if (synth) 14.dp else 0.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    for (r in rows) Text(r, color = th.text, fontSize = 11.tsp(), letterSpacing = 0.44.sp, maxLines = 1)
                }
                Spacer(Modifier.height(8.dp))
            }
        }
    }
}

/** Заголовок окна звука: main заглавными жёлтым, после тире detail и preset цветом текста; до двух строк. */
@Composable
private fun SoundTitle(main: String, detail: String, preset: String, modifier: Modifier) {
    val th = LocalTheme.current
    val text = buildAnnotatedString {
        withStyle(SpanStyle(color = th.accentText)) { append(main.uppercase()) }
        if (detail.isNotEmpty()) withStyle(SpanStyle(color = th.text)) { append(" — $detail") }
        if (preset.isNotEmpty()) withStyle(SpanStyle(color = th.text)) { append(" — $preset") }
    }
    Text(text, fontSize = 13.tsp(), fontWeight = FontWeight.Bold, maxLines = 2, modifier = modifier)
}

/** Форма волны сэмпла окна (WaveStrip.swift): огибающая по модулю (стерео — L и R полосами), отрезанное Start/End
 *  затемнено, полосы воспроизведения — жёлтые линии 2 точки (тают за 1,2 с). */
@Composable
private fun SampleWave(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val w = s.wave
    Box(modifier.onSizeChanged { sz ->
        val cols = 160
        if (cols != s.waveCols) {
            s.waveCols = cols
            s.reload()
        }
    }) {
        androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
            val ch = w.getOrElse(0) { 0f }.toInt()
            if (ch <= 0) return@Canvas
            val cols = (w.size - 3) / ch
            if (cols <= 0) return@Canvas
            val laneH = size.height / ch
            val dx = size.width / cols
            val path = androidx.compose.ui.graphics.Path()
            for (c in 0 until ch) {
                val mid = laneH * (c + 0.5f)
                val amp = laneH / 2 * 0.9f
                fun h(k: Int) = maxOf(0.5f, w[3 + c * cols + k] * amp)
                path.moveTo(0f, mid)
                for (k in 0 until cols) { path.lineTo(k * dx, mid - h(k)); path.lineTo((k + 1) * dx, mid - h(k)) }
                path.lineTo(size.width, mid)
                for (k in cols - 1 downTo 0) { path.lineTo((k + 1) * dx, mid + h(k)); path.lineTo(k * dx, mid + h(k)) }
                path.close()
            }
            drawPath(path, th.text)
            val scrim = th.bg.copy(alpha = 0.6f)
            drawRect(scrim, size = androidx.compose.ui.geometry.Size(size.width * w[1], size.height))
            val x1 = size.width * w[2]
            drawRect(scrim, androidx.compose.ui.geometry.Offset(x1, 0f), androidx.compose.ui.geometry.Size(size.width - x1, size.height))
        }
        val hd = s.heads
        if (hd.isNotEmpty()) androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
            for (k in 0 until hd.size / 2) {
                val x = size.width * hd[2 * k]
                drawRect(th.accent.copy(alpha = hd[2 * k + 1].coerceIn(0f, 1f)), androidx.compose.ui.geometry.Offset(x - density, 0f),
                    androidx.compose.ui.geometry.Size(2 * density, size.height))
            }
        }
    }
}
