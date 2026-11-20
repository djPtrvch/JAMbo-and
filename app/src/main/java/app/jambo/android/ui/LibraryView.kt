package app.jambo.android.ui

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.jambo.android.AppNative
import app.jambo.android.LibAct
import app.jambo.android.MediaDecode
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.security.MessageDigest

/**
 * Браузер библиотеки (LibraryBrowser.swift ветки swift JAMbo) на месте центра экрана: вверху ‹, путь, NEW FOLDER,
 * IMPORT и ✕; список папок, сэмплов и пресетов; в выборе нескольких — SELECT ALL, LOAD, CANCEL; внизу — что и куда
 * загружено. IMPORT — системный выбор файлов (можно несколько): аудио и видео, .jambopreset. Нет пока: SAVE, EXPORT,
 * .zip, перетаскивание папок, LOAD TABLE.
 */
@Composable
fun LibraryView(s: ScreenState, modifier: Modifier) {
    val th = LocalTheme.current
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val info = s.lib
    val rows = s.libRows
    val items = remember(rows) {
        (2 until rows.size - 5 step 6).map { LibItem(rows[it], rows[it + 1], rows[it + 2], rows[it + 3], rows[it + 4] == "1", rows[it + 5] == "1") }
    }
    var openRow by remember { mutableStateOf<String?>(null) }
    var ask by remember { mutableStateOf<Pair<String, String>?>(null) }  // (id или "" — новая папка, имя)
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult
        val folder = AppNative.libImportFolder()
        s.importing = uris.size
        AppNative.libNote("Importing ${uris.size} file${if (uris.size == 1) "" else "s"}…")
        scope.launch {
            val n = withContext(Dispatchers.IO) { importFiles(ctx, uris, folder) { s.importing = it } }
            s.importing = 0
            AppNative.libNote(if (n == 0) "Nothing to import: no audio or presets found" else "Imported $n item${if (n == 1) "" else "s"}")
            s.reload()
        }
    }
    Column(modifier, verticalArrangement = Arrangement.spacedBy(Layout.gap.dp)) {
        Row(Modifier.fillMaxWidth().height(Layout.topHeight.dp), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Tool("‹", Modifier.width(Layout.topHeight.dp), enabled = info[1] == 0) { s.libAct(LibAct.UP) }
            Text(rows[0], color = th.text, fontSize = 13.tsp(), fontWeight = FontWeight.Bold, maxLines = 1,
                overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f).padding(horizontal = 6.dp))
            Tool("NEW FOLDER", Modifier.width(120.dp)) { ask = "" to "New folder" }
            Tool("IMPORT", Modifier.width(96.dp), enabled = s.importing == 0) { pick.launch(arrayOf("*/*")) }
            Tool("✕", Modifier.width(Layout.topHeight.dp)) { s.libAct(LibAct.CLOSE) }
        }
        Box(Modifier.weight(1f).fillMaxWidth().tile(th.panel2)) {
            if (items.isEmpty()) Text(if (rows.size > 0 && rows[0].endsWith("RECORDINGS")) "Stems recorded from each track will be saved here."
                else "Empty folder. IMPORT adds audio files and presets here.", color = th.dim, fontSize = 12.tsp(),
                modifier = Modifier.padding(10.dp))
            else LazyColumn(Modifier.fillMaxSize().padding(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                items(items, key = { it.id }) { n ->
                    LibraryRow(s, n, openRow == n.id, { o -> openRow = if (o) n.id else null }) { ask = n.id to n.name }
                }
            }
        }
        if (info[2] != 0) Row(Modifier.fillMaxWidth().height(48.dp), horizontalArrangement = Arrangement.spacedBy(Layout.gap.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text("${info[3]} selected · tap samples and presets to add or remove", color = th.text, fontSize = 12.tsp(), maxLines = 1,
                overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f).padding(horizontal = 6.dp))
            Tool("SELECT ALL", Modifier.width(110.dp)) { s.libAct(LibAct.PICK_ALL) }
            Tool("LOAD", Modifier.width(110.dp)) { s.libAct(LibAct.LOAD_PICKED) }
            Tool("CANCEL", Modifier.width(110.dp)) { s.libAct(LibAct.CANCEL_PICK) }
        }
        Text(rows.getOrElse(1) { "" }, color = th.dim, fontSize = 12.tsp(), maxLines = 1, modifier = Modifier.padding(horizontal = 6.dp))
    }
    ask?.let { (id, name) ->
        InputDialog(if (id.isEmpty()) "New folder" else "Rename", name, 40, { ask = null }) { text ->
            ask = null
            if (id.isEmpty()) s.libAct(LibAct.NEW_FOLDER, text = text) else s.libAct(LibAct.RENAME, id, text)
        }
    }
}

/** Кнопка строки браузера (ToolButton): жирная, по центру. */
@Composable
private fun Tool(title: String, modifier: Modifier, enabled: Boolean = true, action: () -> Unit) {
    val th = LocalTheme.current
    Box(modifier.fillMaxHeight().tile(th.panel2).then(if (enabled) Modifier.pointerTap(action) else Modifier),
        contentAlignment = Alignment.Center) {
        val icon = title == "‹" || title == "✕"
        Text(title, color = if (enabled) th.text else th.off, fontSize = if (icon) 18.isp() else 13.tsp(),
            fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

/** IMPORT в папку folder (поток IO): WAV, AIFF, CAF и .jambopreset — C++; остальное — MediaCodec. Возвращает число
 *  импортированных. Файлы больше 512 МБ пропускаются. */
private fun importFiles(ctx: Context, uris: List<Uri>, folder: String, left: (Int) -> Unit): Int {
    var n = 0
    uris.forEachIndexed { k, u ->
        left(uris.size - k)
        val name = displayName(ctx, u)
        val bytes = try {
            ctx.contentResolver.openInputStream(u)?.use { st ->
                if (st.available() > 512 shl 20) null else st.readBytes()
            }
        } catch (_: Exception) { null } ?: return@forEachIndexed
        when (AppNative.libImportBytes(name, bytes, folder)) {
            1, 3 -> n++
            2 -> MediaDecode.decode(bytes)?.let { p ->
                if (AppNative.libImportPcm(name, sampleKey(bytes), p.data, p.channels, p.rate, folder) == 1) n++
            }
        }
    }
    return n
}

private fun displayName(ctx: Context, u: Uri): String {
    ctx.contentResolver.query(u, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
        if (c.moveToFirst()) c.getString(0)?.let { return it }
    }
    return u.lastPathSegment?.substringAfterLast('/') ?: "file"
}

/** Ключ сэмпла — как SampleStore у iPad: SHA-256 байтов файла, первые 24 hex-знака. */
private fun sampleKey(b: ByteArray): String =
    MessageDigest.getInstance("SHA-256").digest(b).take(12).joinToString("") { "%02x".format(it) }
