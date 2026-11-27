package app.jambo.android.ui

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.ui.window.Dialog
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import app.jambo.android.LibAct
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

@Composable
fun SampleEditor(s: ScreenState, modifier: Modifier) {
    val theme = LocalTheme.current
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val texts = remember(s.revision) { AppNative.sampleEditorTexts() }
    val key = texts[0]
    val warp = remember(key, s.revision) { WarpSample(AppNative.sampleWarpInfo()) }
    var warpOn by remember(key) { mutableStateOf(false) }
    var warpGrid by remember { mutableStateOf(0.25) }
    var view by remember(key) { mutableStateOf(SampleView()) }
    val wave = remember(key, s.revision, if (warpOn) null else view) {
        AppNative.sampleEditorWave(editorColumns, if (warpOn) 0.0 else view.left, if (warpOn) 1.0 else view.right)
    }
    val labels = remember(key, s.revision) { AppNative.sampleSliceLabels() }
    val roles = remember(key, s.revision) { AppNative.sampleSliceRoles() }
    var roleFlag by remember(key) { mutableIntStateOf(-1) }
    LaunchedEffect(s.heldSlice, key) {
        roleFlag = -1
        if (s.heldSlice >= 0) {
            delay(500)
            roleFlag = s.heldSlice
        }
    }
    DisposableEffect(key, warpOn) { onDispose { s.releaseSampleSlice() } }
    val channels = wave[0].toInt()
    val duration = wave[1]
    val count = wave[2].toInt()
    val isTrack = s.tracks[2] == 0
    var start by remember(key) { mutableFloatStateOf(0f) }
    var end by remember(key) { mutableFloatStateOf(1f) }
    var status by remember { mutableIntStateOf(AppNative.sampleEditorStatus()) }
    var recSources by remember { mutableStateOf(false) }
    var busy by remember { mutableStateOf(false) }
    var notice by remember { mutableStateOf<String?>(null) }
    val mic = remember { SampleMic() }
    var recordingPeaks by remember { mutableStateOf(FloatArray(0)) }
    var recordSince by remember { mutableStateOf(0L) }
    var now by remember { mutableStateOf(0L) }
    val recording = status and 2 != 0
    val preview = status and 1 != 0
    val available = channels > 0 && !busy && !recording
    LaunchedEffect(Unit) {
        while (true) {
            status = AppNative.sampleEditorStatus()
            if (status and 2 != 0) {
                now = android.os.SystemClock.elapsedRealtime()
                recordingPeaks = if (mic.running) mic.peaks() else AppNative.sampleRecordPeaks(editorColumns)
            } else if (recordingPeaks.isNotEmpty()) recordingPeaks = FloatArray(0)
            delay(50)
        }
    }
    BackHandler(enabled = !busy) { s.sampleEditor = false }
    DisposableEffect(Unit) {
        onDispose {
            mic.stop()
            AppNative.sampleEditorPreview(-1.0, 0.0)
        }
    }
    fun began() {
        recordSince = android.os.SystemClock.elapsedRealtime()
        now = recordSince
        status = AppNative.sampleEditorStatus()
    }
    fun startMic() {
        if (!AppNative.sampleRecordStart(-1)) return
        mic.prepare()
        began()
        scope.launch {
            val ok = mic.record { data, frames -> AppNative.sampleRecordMicDone(data, frames) }
            status = AppNative.sampleEditorStatus()
            s.refresh()
            if (!ok) notice = "Could not record from the microphone."
        }
    }
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) startMic() else notice = "Microphone access is required to record from the audio input."
    }
    fun edit(action: Int) {
        busy = true
        val a = start.toDouble()
        val b = end.toDouble()
        scope.launch {
            withContext(Dispatchers.IO) { AppNative.sampleEditorEdit(action, a, b) }
            s.refresh()
            busy = false
        }
    }
    Column(modifier, verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        Row(Modifier.fillMaxWidth().height(Layout.topHeight.dp), Arrangement.spacedBy(Layout.gap.dp),
            Alignment.CenterVertically) {
            val elapsed = ((now - recordSince) / 1000).coerceIn(0, 60)
            val label = when {
                recording -> "REC · ${elapsed}s / 60s"
                busy -> "Processing…"
                channels == 0 -> "SAMPLE EDITOR"
                else -> texts[1] + "\n" + String.format(Locale.ROOT, "%.2f / %.2f s", duration * (end - start), duration)
            }
            Text(label, Modifier.weight(2f).padding(horizontal = 6.dp), color = theme.text,
                fontSize = 11.tsp(), maxLines = 2, overflow = TextOverflow.Ellipsis)
            Tool(if (preview) "■" else "▶", Modifier.weight(1f), available) {
                if (warpOn) AppNative.sampleWarpPreview()
                else AppNative.sampleEditorPreview(start.toDouble(), end.toDouble())
                status = AppNative.sampleEditorStatus()
            }
            Tool(if (recording) "■ REC" else "REC", Modifier.weight(1f), isTrack && !busy || recording) {
                if (recording) {
                    if (mic.running) mic.stop() else AppNative.sampleRecordStop()
                } else recSources = true
            }
            val flag = roleFlag
            if (flag >= 0 && s.heldSlice == flag && !warpOn) {
                for ((role, label) in sliceRoleNames) {
                    InkButton(label, roles.getOrNull(flag) == role, Modifier.weight(1f), Layout.topHeight) {
                        s.selectSampleSliceRole(flag, role)
                    }
                }
            } else {
                Box(
                    Modifier.weight(1f).fillMaxHeight().tile(if (warpOn) theme.accent else theme.panel2)
                        .then(if (available) Modifier.pointerInput(key, warpOn, view, warp) {
                            detectTapGestures(
                                onTap = {
                                    if (preview) AppNative.sampleEditorPreview(-1.0, 0.0)
                                    if (!warpOn) {
                                        s.warp(WarpAct.ENTER)
                                        val next = WarpSample(AppNative.sampleWarpInfo())
                                        val total = next.endBeat - next.firstBeat
                                        val lo = (next.beatOf(view.left) - next.firstBeat) / total
                                        val hi = (next.beatOf(view.right) - next.firstBeat) / total
                                        view = SampleView(lo, (hi - lo).coerceAtLeast(1e-9))
                                    } else {
                                        val total = warp.endBeat - warp.firstBeat
                                        val lo = warp.sourceAt(warp.firstBeat + view.left * total).coerceIn(0.0, 1.0)
                                        val hi = warp.sourceAt(warp.firstBeat + view.right * total).coerceIn(lo, 1.0)
                                        view = if (hi > lo) SampleView(lo, hi - lo) else SampleView()
                                    }
                                    warpOn = !warpOn
                                    status = AppNative.sampleEditorStatus()
                                },
                                onLongPress = { s.warp(WarpAct.BYPASS) },
                            )
                        } else Modifier),
                    contentAlignment = Alignment.Center,
                ) {
                    Text("WARP", color = if (!available) theme.off else if (warpOn) theme.onAccent else theme.text,
                        fontSize = 13.tsp(), textDecoration = if (warp.off) TextDecoration.LineThrough else null)
                }
                if (warpOn) WarpTools(s, warp, warpGrid, { warpGrid = it }, available)
                else {
                    Tool("MONO", Modifier.weight(1f), available && channels == 2) { edit(2) }
                    Tool("CUT", Modifier.weight(1f), available && (start > 0f || end < 1f)) { edit(3) }
                    Tool("TRIM", Modifier.weight(1f), available && (start > 0f || end < 1f)) { edit(0) }
                    Tool("NORMALIZE", Modifier.weight(1.5f), available) { edit(1) }
                    Tool("CLEAR", Modifier.weight(1f), available) { edit(4) }
                }
            }
            Tool("LOAD", Modifier.weight(1f), isTrack && !busy && !recording) { s.libAct(LibAct.OPEN) }
            Tool("SAVE", Modifier.weight(1f), available) { AppNative.sampleEditorSave(); s.refresh() }
            Tool("✕", Modifier.weight(0.7f), !busy) { s.sampleEditor = false }
        }
        Box(Modifier.weight(1f).fillMaxWidth().tile(theme.panel), contentAlignment = Alignment.Center) {
            when {
                recording -> CaptureWave(recordingPeaks)
                channels > 0 && warpOn -> WarpWave(s, key, warp, warpGrid, channels, editorColumns, view) { view = it }
                channels > 0 -> EditorWave(s, key, wave, count, labels, roles, warp, view, { view = it }, start, end, s.heads, !busy,
                    { a, b -> start = a; end = b }, { index, at ->
                        AppNative.sampleEditorSlice(index, at.toDouble())
                        s.refresh()
                    })
                else -> Text(if (isTrack) "No sample on this track — LOAD a file or REC."
                    else "Select a track to edit its sample.", color = theme.dim, fontSize = 13.tsp())
            }
        }
    }
    if (recSources) {
        val sources = listOf("MICROPHONE" to -1, "MASTER" to -2) +
            s.trackNames.mapIndexed { index, name -> name to index }
        Dialog(onDismissRequest = { recSources = false }) {
            Column(Modifier.width(320.dp).fillMaxHeight(0.7f).tile(theme.bg).padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                Text("Record from", color = theme.text, fontSize = 14.tsp())
                LazyColumn(verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
                    items(sources) { (name, source) ->
                        Box(Modifier.fillMaxWidth().height(36.dp)) {
                            Tool(name, Modifier.fillMaxWidth()) {
                                recSources = false
                                if (source == -1 && ctx.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
                                    permission.launch(Manifest.permission.RECORD_AUDIO)
                                else if (source == -1) startMic()
                                else if (AppNative.sampleRecordStart(source)) began()
                                else notice = "Could not start recording."
                            }
                        }
                    }
                }
            }
        }
    }
    notice?.let { message ->
        ChoiceDialog(message, null, listOf(Triple("OK", true) { notice = null })) { notice = null }
    }
}

internal const val editorColumns = 640

@Composable
private fun CaptureWave(peaks: FloatArray) {
    val theme = LocalTheme.current
    Canvas(Modifier.fillMaxSize()) {
        val count = peaks.size
        val center = size.height / 2
        drawLine(theme.dim, Offset(0f, center), Offset(size.width, center))
        peaks.forEachIndexed { i, peak ->
            val x = size.width * (i + 0.5f) / count
            val amplitude = peak.coerceIn(0f, 1f) * center
            drawLine(theme.rec, Offset(x, center - amplitude), Offset(x, center + amplitude))
        }
    }
}
