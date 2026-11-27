package app.jambo.android.ui

import android.content.ActivityNotFoundException
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import java.io.File
import java.io.IOException
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

internal data class SongOptions(
    val m4a: Boolean = false, val quality: Int = 1, val depth: Int = 24, val rate: Int = 48000,
    val tail: Int = -1,
) {
    val codec get() = if (m4a) quality + 1 else 0
    val effectiveDepth get() = if (m4a) depth.coerceAtMost(24) else depth
}

internal class SongRequest(val options: SongOptions, val name: String) {
    val cancelled = AtomicBoolean()
}

internal class SongExportUi {
    var options by mutableStateOf(SongOptions())
    var request by mutableStateOf<SongRequest?>(null)
    var progress by mutableStateOf(0f)
    var saving by mutableStateOf(false)
    var error by mutableStateOf<String?>(null)

    fun start(name: String) {
        if (saving || request != null) return
        progress = 0f
        error = null
        request = SongRequest(options, name)
    }

    fun cancel() {
        request?.cancelled?.set(true)
        AppNative.cancelSong()
    }
}

private data class SavedSong(val file: File)

@Composable
internal fun SongExportAnchor(s: ScreenState) {
    val context = LocalContext.current
    val state = s.song
    val scope = rememberCoroutineScope()
    var pending by remember { mutableStateOf<SavedSong?>(null) }
    fun save(uri: Uri?) {
        val prepared = pending ?: return
        pending = null
        if (uri == null) {
            prepared.file.delete()
            state.saving = false
            return
        }
        scope.launch {
            try {
                withContext(Dispatchers.IO + NonCancellable) {
                    val out = context.contentResolver.openOutputStream(uri, "wt") ?: throw IOException()
                    out.use { stream -> prepared.file.inputStream().use { it.copyTo(stream) } }
                }
            } catch (_: IOException) {
                state.error = "Could not save the song."
            } catch (_: SecurityException) {
                state.error = "Could not save the song."
            } finally {
                prepared.file.delete()
                state.saving = false
            }
        }
    }

    val wavePicker = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav"), ::save)
    val m4aPicker = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/mp4"), ::save)
    val request = state.request
    LaunchedEffect(request) {
        if (request == null) return@LaunchedEffect
        val ext = if (request.options.m4a) ".m4a" else ".wav"
        var temp: File? = null
        try {
            val result = withContext(Dispatchers.IO + NonCancellable) {
                if (request.cancelled.get()) return@withContext 0
                val file = File.createTempFile("jambo-song-", ext, context.cacheDir)
                temp = file
                if (!AppNative.prepareSong()) return@withContext -1
                if (request.cancelled.get()) AppNative.cancelSong()
                val fmt = request.options
                AppNative.renderSongFile(file.path, fmt.codec, fmt.effectiveDepth, fmt.rate, fmt.tail)
            }
            s.refresh()
            if (result > 0 && !request.cancelled.get()) {
                val file = temp ?: throw IOException()
                val title = request.name.replace('/', '-').replace('\\', '-').ifBlank { "Song" } + ext
                pending = SavedSong(file)
                state.saving = true
                try {
                    if (request.options.m4a) m4aPicker.launch(title) else wavePicker.launch(title)
                } catch (_: ActivityNotFoundException) {
                    pending = null
                    state.saving = false
                    state.error = "No system file picker is available."
                }
            } else if (result < 0 && !request.cancelled.get()) {
                state.error = "Can’t export the song."
            }
        } catch (_: IOException) {
            state.error = "Can’t export the song."
        } finally {
            if (pending?.file != temp) temp?.delete()
            state.request = null
        }
    }
    LaunchedEffect(request) {
        if (request == null) return@LaunchedEffect
        delay(100)
        while (state.request === request) {
            val next = AppNative.songProgress() / 1000f
            if (next < 1f || state.progress > 0f) state.progress = next
            delay(80)
        }
    }
    val error = state.error
    if (error != null && !s.mainMenu) {
        ChoiceDialog(error, null, listOf(Triple("OK", true) { state.error = null })) { state.error = null }
    }
}

@Composable
internal fun SongExportPage(s: ScreenState, back: () -> Unit) {
    val ui = s.song
    val fmt = ui.options
    val canEdit = ui.request == null && !ui.saving
    val seconds = remember(s.revision) { AppNative.songSeconds() }
    val theme = LocalTheme.current
    Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        MenuBackHeader("EXPORT SONG", back)
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(end = 6.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            SongChoices("FORMAT", listOf("WAV", "M4A"), if (fmt.m4a) 1 else 0, canEdit) {
                ui.options = fmt.copy(m4a = it == 1)
            }
            if (fmt.m4a) SongChoices("QUALITY", listOf("AAC 256", "AAC 320", "LOSSLESS"), fmt.quality, canEdit) {
                ui.options = fmt.copy(quality = it)
            }
            if (!fmt.m4a || fmt.quality == 2) {
                val depths = if (fmt.m4a) listOf("16", "24") else listOf("16", "24", "32 FLOAT")
                SongChoices("BIT DEPTH", depths, (fmt.effectiveDepth - 16) / 8, canEdit) {
                    ui.options = fmt.copy(depth = 16 + 8 * it)
                }
            }
            SongChoices("SAMPLE RATE", listOf("44.1 KHZ", "48 KHZ"), if (fmt.rate == 44100) 0 else 1, canEdit) {
                ui.options = fmt.copy(rate = if (it == 0) 44100 else 48000)
            }
            val tails = listOf(-1, 0, 2, 5, 10)
            SongChoices("TAIL", listOf("AUTO", "OFF", "2 S", "5 S", "10 S"), tails.indexOf(fmt.tail), canEdit) {
                ui.options = fmt.copy(tail = tails[it])
            }
            if (seconds <= 0) MenuNote("The arrangement is empty: put clips on the timeline first.")
            else {
                val time = seconds.roundToInt()
                val label = String.format(Locale.ROOT, "%d:%02d", time / 60, time % 60)
                val tail = when (fmt.tail) {
                    -1 -> "plus effect tails until silence (up to 10 s)"
                    0 -> "cut at the end of the last block"
                    else -> "plus ${fmt.tail} s of tail"
                }
                MenuNote("The whole arrangement from the start, $label, $tail. Loop, REF and MIDI out are off while exporting.")
            }
            if (ui.request != null) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    BoxWithConstraints(Modifier.weight(1f).height(9.dp).tile(theme.meterTrack, 3f)) {
                        Box(Modifier.width(maxWidth * ui.progress.coerceIn(0f, 1f)).height(9.dp).tile(theme.accent, 3f))
                    }
                    MenuItem("CANCEL", Modifier.width(110.dp)) { ui.cancel() }
                }
            } else {
                MenuItem("EXPORT", enabled = canEdit && seconds > 0) { ui.start(s.projectRows.lastOrNull().orEmpty()) }
            }
            ui.error?.let { MenuNote(it) }
        }
    }
}

@Composable
private fun SongChoices(title: String, names: List<String>, selected: Int, enabled: Boolean, pick: (Int) -> Unit) {
    MenuHead(title)
    MenuChoices(names.mapIndexed { i, text -> text to (i == selected) }, enabled = List(names.size) { enabled }, pick = pick)
}
