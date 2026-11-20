package app.jambo.android.ui

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jambo.android.AppNative
import app.jambo.android.AppNative.Act
import kotlin.math.abs
import kotlin.math.log2
import kotlin.math.pow
import kotlin.math.roundToInt

/** Типы полос по порядку меню и поля TYPE: low cut, high cut, bell, low shelf, high shelf, notch. */
val EQ_TYPE_ORDER = intArrayOf(3, 4, 0, 1, 2, 5)
val EQ_TYPES = arrayOf("Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch")
val EQ_SLOPES = intArrayOf(12, 24, 48)
val EQ_MS = arrayOf("STEREO", "MID", "SIDE")
fun eqHasGain(type: Int) = type <= 2
fun eqIsCut(type: Int) = type == 3 || type == 4

/** Меню EQ: полосы (TYPE), M/S, DYN; ввод числом. */
class EqUi {
    var bandMenu by mutableStateOf(false)
    var msMenu by mutableStateOf(false)
    var dynMenu by mutableStateOf(false)
    /** Ввод числом: 0 FREQ, 1 GAIN, 2 Q, 3 THRESHOLD, 4 RANGE (−1 — нет). */
    var input by mutableStateOf(-1)
    var plot by mutableStateOf(Rect.Zero)
}

/** EQ на месте центра (EqView.swift): вверху строка полей, под ней поле EQ; справа остаётся измеритель. Канал —
 *  выбранная дорожка, при MIX и MST — мастер. LISTEN, PRESETS, сравнение дорожек и коррекция наушников — позже. */
@Composable
fun EqView(s: ScreenState, modifier: Modifier) {
    val ui = s.eqUi
    var origin by remember { mutableStateOf(0f) }
    val d = LocalDensity.current.density
    Column(modifier.onGloballyPositioned { c -> s.body?.let { b -> if (b.isAttached) origin = b.localBoundingBoxOf(c).left / d } },
        verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        EqBar(s, ui, origin, Modifier.fillMaxWidth().height(48.dp))
        EqPlot(s, Modifier.fillMaxWidth().weight(1f).onGloballyPositioned { c ->
            s.body?.let { b -> if (b.isAttached) ui.plot = b.localBoundingBoxOf(c) }
        })
    }
}

/** Строка EQ (EqBar.swift): имя канала под бутербродом, TYPE…Q — равные до правого края UNDO, M/S и DYN — поровну до
 *  кнопки EQ, LISTEN, PRESETS, BYPASS — под EQ, PATTERNS, ARRANGE. */
@Composable
private fun EqBar(s: ScreenState, ui: EqUi, origin: Float, modifier: Modifier) {
    val d = LocalDensity.current.density
    val th = LocalTheme.current
    val t = s.eqTexts
    val k = s.eq[3]
    val sel = k >= 0
    val type = if (sel) s.eq[5 + 6 * k + 2] else 0
    val r = s.topRects
    val g = Layout.gap
    val x0 = 52 + g
    fun rx(key: String) = (r[key]?.left ?: 0f) / d - origin
    fun rw(key: String) = (r[key]?.width ?: 0f) / d
    val qRight = (r["undo"]?.right ?: 0f) / d - origin
    val w4 = maxOf(0f, (qRight - x0 - 3 * g) / 4)
    val listen = rx("eq")
    val w2 = maxOf(0f, (listen - qRight - 3 * g) / 2)
    Box(modifier) {
        Box(Modifier.width(52.dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
            Text("${t[0]}\nEQ", color = th.dim, fontSize = 11.sp, letterSpacing = 0.9.sp, textAlign = TextAlign.Center, maxLines = 2)
        }
        val at = { x: Float, w: Float -> Modifier.offset(x.dp, 0.dp).width(w.dp).fillMaxHeight() }
        val bands = s.eqBands
        EqField("TYPE", t[1], sel, 30f, at(x0, w4), { -EQ_TYPE_ORDER.indexOf(type).toDouble() },
            { v -> val n = EQ_TYPE_ORDER.size; val i = ((-v.toInt()) % n + n) % n; s.act(Act.SET_EQ_TYPE, k, EQ_TYPE_ORDER[i]) },
            tap = { ui.bandMenu = true })
        EqField("FREQ", t[2], sel, 2f, at(x0 + (w4 + g), w4), { (log2(bands[5 * k] / 20.0) * 24).roundToInt().toDouble() },
            { v -> AppNative.eqSetBand(k, 20.0 * 2.0.pow(v / 24), Double.NaN, Double.NaN); s.refresh() },
            double = { ui.input = 0 })
        val hasGain = sel && eqHasGain(type)
        EqField("GAIN", t[3], hasGain, 1f, at(x0 + 2 * (w4 + g), w4), { (bands[5 * k + 1] * 10.0).roundToInt().toDouble() },
            { v -> AppNative.eqSetBand(k, Double.NaN, v / 10, Double.NaN); s.refresh() }, double = { ui.input = 1 })
        EqField("Q", t[4], sel, 3f, at(x0 + 3 * (w4 + g), w4), { (log2(bands[5 * k + 2].toDouble()) * 12).roundToInt().toDouble() },
            { v -> AppNative.eqSetBand(k, Double.NaN, Double.NaN, 2.0.pow(v / 12)); s.refresh() }, double = { ui.input = 2 })
        val ms = if (sel) s.eq[5 + 6 * k + 4] else 0
        EqField("M/S", t[5], sel, 30f, at(qRight + g, w2), { -ms.toDouble() },
            { v -> s.act(Act.SET_EQ_MS, k, ((-v.toInt()) % 3 + 3) % 3) }, tap = { ui.msMenu = true })
        EqField("DYN", t[6], hasGain, 100000f, at(qRight + 2 * g + w2, w2), { 0.0 }, { }, tap = { ui.dynMenu = true })
        EqTool("LISTEN", false, false, at(listen, rw("eq"))) { }
        EqTool("PRESETS", false, false, at(rx("patterns"), rw("patterns"))) { }
        EqTool("BYPASS", true, s.eq[2] == 0, at(rx("arrange"), rw("arrange"))) { s.act(Act.TOGGLE_EQ_BYPASS) }
    }
}

/** Поле строки EQ: подпись над значением; палец вверх-вниз — pxPerUnit на пункт (одна запись UNDO на жест), тап,
 *  двойной тап без движения — ввод числом; выключенное — серое, касаний нет. */
@Composable
private fun EqField(
    label: String, value: String, enabled: Boolean, pxPerUnit: Float, modifier: Modifier, get: () -> Double,
    set: (Double) -> Unit, tap: (() -> Unit)? = null, double: (() -> Unit)? = null, moveTol: Float = 10f,
) {
    val th = LocalTheme.current
    var active by remember { mutableStateOf(false) }
    val getter by rememberUpdatedState(get)
    val setter by rememberUpdatedState(set)
    val onTap by rememberUpdatedState(tap)
    val onDouble by rememberUpdatedState(double)
    var lastTap by remember { mutableStateOf(0L) }
    Column(
        modifier.tile(if (active) th.panel2 else th.panel).padding(horizontal = 12.dp)
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                awaitEachGesture {
                    val down = awaitFirstDown()
                    active = true
                    AppNative.act(Act.BEGIN_GESTURE)
                    val start = getter()
                    var moved = false
                    while (true) {
                        val ev = awaitPointerEvent()
                        val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                        val dy = (c.position.y - down.position.y) / density
                        if (!moved && abs(dy) > moveTol) moved = true
                        if (moved) setter(start + (-dy / pxPerUnit).roundToInt())
                        c.consume()
                        if (!c.pressed) break
                    }
                    AppNative.act(Act.END_GESTURE)
                    active = false
                    if (!moved) {
                        val now = System.currentTimeMillis()
                        val dbl = onDouble
                        if (dbl != null && now - lastTap < 320) {
                            lastTap = 0
                            dbl()
                        } else {
                            lastTap = now
                            onTap?.invoke()
                        }
                    }
                }
            },
        verticalArrangement = Arrangement.Center,
    ) {
        Text(label, color = th.dim, fontSize = 9.sp, letterSpacing = 0.8.sp)
        Text(value, color = if (enabled) th.text else th.off, fontSize = 16.sp, maxLines = 1)
    }
}

/** Кнопка строки EQ (LISTEN, PRESETS, BYPASS): горит, когда active; выключенная — серая. */
@Composable
private fun EqTool(title: String, enabled: Boolean, active: Boolean, modifier: Modifier, action: () -> Unit) {
    val th = LocalTheme.current
    val fg = if (!enabled) th.off else if (active) th.onAccent else th.text
    Box(modifier.tile(if (active) th.accent else th.panel2).then(if (enabled) Modifier.pointerTap(action) else Modifier),
        contentAlignment = Alignment.Center) {
        Text(title, color = fg, fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.8.sp, maxLines = 1)
    }
}

/** Меню полосы (типы, крутизна среза, ON/OFF, DELETE), M/S, DYN; ввод числом. Тап мимо — закрыть. */
@Composable
fun EqMenus(s: ScreenState, ui: EqUi) {
    val d = LocalDensity.current.density
    val k = s.eq[3]
    if (k < 0) return
    val base = 5 + 6 * k
    val type = s.eq[base + 2]
    val plot = ui.plot
    if (ui.bandMenu) {
        Box(Modifier.fillMaxSize().pointerTap { ui.bandMenu = false })
        val pick = { f: () -> Unit -> f(); ui.bandMenu = false }
        Column(Modifier.offset((plot.left / d + 8).dp, (plot.top / d + 8).dp).width(390.dp).menuBox()
            .pointerInput(Unit) { detectTapGestures { } }.padding(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            for (r in 0 until 2) Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (c in 0 until 3) {
                    val t = EQ_TYPE_ORDER[r * 3 + c]
                    MenuItem(EQ_TYPES[t].uppercase(), Modifier.weight(1f), current = type == t) { pick { s.act(Act.SET_EQ_TYPE, k, t) } }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (sl in 0 until 3) {
                    val cut = eqIsCut(type)
                    MenuItem("${EQ_SLOPES[sl]} dB/oct", Modifier.weight(1f), enabled = cut, current = cut && s.eq[base + 3] == sl) {
                        pick { s.act(Act.SET_EQ_SLOPE, k, sl) }
                    }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem(if (s.eq[base + 1] == 0) "ON" else "OFF", Modifier.weight(1f)) { pick { s.act(Act.TOGGLE_EQ_BAND, k) } }
                MenuItem("DELETE", Modifier.weight(1f), red = true) { pick { s.act(Act.DELETE_EQ_BAND, k) } }
            }
        }
    }
    if (ui.msMenu) {
        Box(Modifier.fillMaxSize().pointerTap { ui.msMenu = false })
        Column(Modifier.offset((plot.left / d + 8).dp, (plot.top / d + 8).dp).width(200.dp).menuBox()
            .pointerInput(Unit) { detectTapGestures { } }.padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            MenuHead("CHANNEL")
            for (i in 0 until 3) MenuItem(EQ_MS[i], current = s.eq[base + 4] == i) { s.act(Act.SET_EQ_MS, k, i); ui.msMenu = false }
        }
    }
    if (ui.dynMenu) EqDynMenu(s, ui, k)
    if (ui.input >= 0) {
        val b = s.eqBands
        val titles = arrayOf("FREQ (Hz, 1.5k — kHz)", "GAIN (dB)", "Q", "THRESHOLD (dB)", "RANGE (dB)")
        val init = when (ui.input) {
            0 -> "${b[5 * k].roundToInt()}"
            1 -> "%.1f".format(java.util.Locale.US, b[5 * k + 1])
            2 -> "%.2f".format(java.util.Locale.US, b[5 * k + 2])
            3 -> "%.1f".format(java.util.Locale.US, b[5 * k + 3])
            else -> "%.1f".format(java.util.Locale.US, b[5 * k + 4])
        }
        val field = ui.input
        InputDialog(titles[field], init, 16, cancel = { ui.input = -1 }) { text ->
            ui.input = -1
            val v = if (field == 0) parseFreq(text) else text.trim().replace(',', '.').toDoubleOrNull()
            if (v != null) when (field) {
                0 -> AppNative.eqSetBand(k, v, Double.NaN, Double.NaN)
                1 -> AppNative.eqSetBand(k, Double.NaN, v, Double.NaN)
                2 -> if (v > 0) AppNative.eqSetBand(k, Double.NaN, Double.NaN, v)
                3, 4 -> AppNative.eqSetDyn(k, if (field == 3) v else Double.NaN, if (field == 4) v else Double.NaN)
            }
            s.refresh()
            Unit
        }
    }
}

/** Частота из текста: «440», «440 Hz», «1.5k», «2 kHz»; null — не число. */
fun parseFreq(text: String): Double? {
    val t = text.trim().lowercase().replace(',', '.')
    val digits = t.takeWhile { it.isDigit() || it == '.' }
    var v = digits.toDoubleOrNull() ?: return null
    if (v <= 0) return null
    if (t.drop(digits.length).trim().startsWith("k")) v *= 1000
    return v
}

/** Меню DYN: источник — OFF, SELF или дорожка (2 столбца), под ним THRESHOLD и RANGE (тап — ввод числом). */
@Composable
private fun EqDynMenu(s: ScreenState, ui: EqUi, k: Int) {
    val d = LocalDensity.current.density
    val plot = ui.plot
    val names = s.trackNames
    val dyn = s.eq[5 + 6 * k + 5]
    val b = s.eqBands
    Box(Modifier.fillMaxSize().pointerTap { ui.dynMenu = false })
    BoxWithConstraints(Modifier.offset((plot.left / d + 8).dp, (plot.top / d + 8).dp).width(300.dp)
        .height((plot.height / d - 16).dp)) {
        Column(Modifier.fillMaxSize().menuBox().pointerInput(Unit) { detectTapGestures { } }.padding(8.dp)
            .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(4.dp)) {
            MenuHead("DYNAMIC")
            val items = listOf("OFF" to -1, "SELF" to 0) + names.mapIndexed { i, n -> n to i + 1 }
            for (r in 0 until (items.size + 1) / 2) Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (c in 0 until 2) {
                    val it = items.getOrNull(r * 2 + c)
                    if (it != null) MenuItem(it.first, Modifier.weight(1f), current = dyn == it.second) { s.act(Act.SET_EQ_DYN, k, it.second) }
                    else Box(Modifier.weight(1f))
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MenuItem("THRESHOLD ${"%.1f".format(java.util.Locale.US, b[5 * k + 3])} dB", Modifier.weight(1f)) { ui.input = 3 }
                MenuItem("RANGE ${"%+.1f".format(java.util.Locale.US, b[5 * k + 4])} dB", Modifier.weight(1f)) { ui.input = 4 }
            }
        }
    }
}
