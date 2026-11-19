package app.jambo.android

import android.annotation.SuppressLint
import android.os.Build
import android.view.MotionEvent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.material3.Text
import kotlinx.coroutines.delay
import kotlin.math.floor

private val Bg = Color(0xFF000000)
private val Panel = Color(0xFF1C1C1C)
private val Fg = Color(0xFFFFFFFF)
private val Dim = Color(0xFF8A8A8A)
private val Accent = Color(0xFFFFD000)
private val Red = Color(0xFFFF2B2B)

@Composable
fun SpikeScreen(midi: MidiHub, onScanBle: () -> Unit, onLink: (Boolean) -> Unit) {
    var audio by remember { mutableStateOf(DoubleArray(18)) }
    var link by remember { mutableStateOf(DoubleArray(5)) }
    var appCpu by remember { mutableStateOf(0.0) }
    val cpu = remember { CpuMeter() }
    LaunchedEffect(Unit) {
        var n = 0
        while (true) {
            audio = Native.audioInfo()
            val latUs = if (audio[10] > 0) (audio[10] * 1000).toLong() else 0L
            link = Native.linkInfo(latUs)
            if (++n % 10 == 0) appCpu = cpu.sample()
            delay(50)
        }
    }
    Row(
        Modifier.fillMaxSize().background(Bg).padding(16.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        AudioColumn(audio, appCpu, Modifier.weight(1f))
        MidiColumn(midi, onScanBle, Modifier.weight(1.3f))
        LinkColumn(link, onLink, Modifier.weight(1f))
    }
}

@Composable
private fun Title(text: String) =
    Text(text, color = Fg, fontSize = 13.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)

@Composable
private fun Btn(label: String, on: Boolean = false, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Box(
        modifier.height(48.dp)
            .background(if (on) Accent else Panel, RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp),
        contentAlignment = Alignment.Center
    ) { Text(label, color = if (on) Color.Black else Fg, fontWeight = FontWeight.Bold, fontSize = 14.sp) }
}

@Composable
private fun Stat(name: String, value: String, warn: Boolean = false) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text(name, color = Dim, fontSize = 12.sp, modifier = Modifier.weight(1f))
        Text(value, color = if (warn) Red else Fg, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
    }
}

private fun f1(v: Double) = if (v < 0) "—" else "%.1f".format(v)

@SuppressLint("ClickableViewAccessibility")
@OptIn(ExperimentalComposeUiApi::class)
@Composable
private fun AudioColumn(a: DoubleArray, appCpu: Double, modifier: Modifier) {
    var tone by remember { mutableStateOf(false) }
    var flash by remember { mutableStateOf(false) }
    Column(modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Title("AUDIO  · build ${BuildConfig.VERSION_NAME}")
        Btn("TONE", tone, Modifier.fillMaxWidth()) { tone = !tone; Native.setTone(tone) }
        // TAP: звук на касании (ACTION_DOWN), не на отпускании.
        Box(
            Modifier.fillMaxWidth().height(140.dp)
                .background(if (flash) Accent else Panel, RoundedCornerShape(6.dp))
                .pointerInteropFilter { e ->
                    val m = e.actionMasked
                    if (m == MotionEvent.ACTION_DOWN || m == MotionEvent.ACTION_POINTER_DOWN) {
                        val t = if (Build.VERSION.SDK_INT >= 34) e.eventTimeNanos else e.eventTime * 1_000_000L
                        Native.tap(t); flash = true
                    } else if (m == MotionEvent.ACTION_UP || m == MotionEvent.ACTION_CANCEL) flash = false
                    true
                },
            contentAlignment = Alignment.Center
        ) { Text("TAP", color = if (flash) Color.Black else Fg, fontSize = 22.sp, fontWeight = FontWeight.Bold) }
        Column(Modifier.border(1.dp, Panel, RoundedCornerShape(6.dp)).padding(10.dp)) {
            if (a[0] < 1) { Text("Audio stream closed", color = Red); return@Column }
            val api = if (a[6] == 2.0) "AAudio" else "OpenSL ES"
            Stat("Sample rate", "%.0f Hz".format(a[1]))
            Stat("Burst", "%.0f frames".format(a[2]))
            Stat("Buffer", "%.0f / %.0f frames (%s ms)".format(a[3], a[4], f1(a[17])))
            Stat("Callback size", "%.0f frames".format(a[5]))
            Stat("Output latency", "${f1(a[10])} ms")
            Stat("Touch → sound (est.)", "${f1(a[14])} ms")
            Stat("Path", api)
            Stat("Sharing", if (a[7] == 1.0) "EXCLUSIVE" else "SHARED", warn = a[7] != 1.0)
            Stat("Low latency", if (a[8] == 1.0) "YES" else "NO", warn = a[8] != 1.0)
            Stat("MMAP", if (a[9] == 1.0) "YES" else "NO")
            Stat("XRuns", "%.0f".format(a[13]), warn = a[13] > 0)
            Stat("CPU callback avg / max", "${f1(a[11])} / ${f1(a[12])} %")
            Stat("CPU app (1 core)", "%.1f %%".format(appCpu))
            Stat("Device id · ch", "%.0f · %.0f".format(a[15], a[16]))
        }
    }
}

@Composable
private fun MidiColumn(midi: MidiHub, onScanBle: () -> Unit, modifier: Modifier) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Title("MIDI")
        if (!midi.available) { Text("android.media.midi unavailable", color = Red); return@Column }
        if (midi.devices.isEmpty()) Text("No MIDI devices", color = Dim, fontSize = 13.sp)
        midi.devices.forEach { d ->
            Stat("${d.type}  ${d.name}", "in ${d.inPorts} · out ${d.outPorts}${if (d.open) " · open" else ""}")
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Btn(if (midi.scanning.value) "SCANNING…" else "SCAN BLUETOOTH", midi.scanning.value) { onScanBle() }
            Text("Clock ticks: ${midi.clockTicks.value}", color = Dim, fontSize = 12.sp)
        }
        midi.ble.forEach { b ->
            Text("⟶ ${b.name}", color = Accent, fontSize = 14.sp,
                modifier = Modifier.fillMaxWidth().clickable { midi.connectBle(b.address) }.padding(vertical = 8.dp))
        }
        Title("LOG")
        LazyColumn(Modifier.fillMaxWidth().weight(1f).background(Panel, RoundedCornerShape(6.dp)).padding(8.dp)) {
            items(midi.log) { Text(it, color = Fg, fontSize = 11.sp, fontFamily = FontFamily.Monospace) }
        }
    }
}

@Composable
private fun LinkColumn(l: DoubleArray, onLink: (Boolean) -> Unit, modifier: Modifier) {
    var on by remember { mutableStateOf(false) }
    var click by remember { mutableStateOf(false) }
    Column(modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Title("ABLETON LINK")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Btn("LINK", on, Modifier.weight(1f)) { on = !on; onLink(on) }
            Btn("CLICK", click, Modifier.weight(1f)) { click = !click; Native.setLinkClick(click) }
        }
        val tempo = l[2]
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Btn("−") { Native.linkSetTempo((tempo - 1).coerceAtLeast(20.0)) }
            Text("%.2f BPM".format(tempo), color = Fg, fontSize = 22.sp, fontFamily = FontFamily.Monospace,
                textAlign = TextAlign.Center, modifier = Modifier.weight(1f))
            Btn("+") { Native.linkSetTempo((tempo + 1).coerceAtMost(999.0)) }
        }
        Stat("Peers", "%.0f".format(l[1]))
        Stat("Beat", "%.2f".format(l[3]))
        Stat("Phase (quantum 4)", "%.2f".format(l[4]))
        // Фаза: 4 доли такта, горит текущая.
        val beat = floor(l[4]).toInt().coerceIn(0, 3)
        Row(Modifier.fillMaxWidth().height(28.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            repeat(4) { i ->
                Box(Modifier.weight(1f).fillMaxHeight()
                    .background(if (on && i == beat) Accent else Panel, RoundedCornerShape(4.dp)))
            }
        }
        Text("Phase and click include output latency.", color = Dim, fontSize = 12.sp)
    }
}
