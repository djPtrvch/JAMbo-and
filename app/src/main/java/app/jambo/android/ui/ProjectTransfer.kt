package app.jambo.android.ui

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import app.jambo.android.AppNative
import app.jambo.android.ProjectAct
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.IOException

private data class ExportPack(val file: File, val missing: Int)
private data class ImportedPack(val id: String, val name: String, val missing: Int)

@Composable
internal fun ProjectFileButtons(s: ScreenState, name: String, close: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf<String?>(null) }
    var pending by remember { mutableStateOf<ExportPack?>(null) }
    var notice by remember { mutableStateOf<String?>(null) }
    var imported by remember { mutableStateOf<ImportedPack?>(null) }

    val saveDocument = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream"),
    ) { uri ->
        val pack = pending
        pending = null
        if (pack != null && uri != null) {
            scope.launch {
                try {
                    val ok = withContext(Dispatchers.IO + NonCancellable) {
                        try {
                            val output = context.contentResolver.openOutputStream(uri, "wt") ?: throw IOException()
                            output.use { stream -> pack.file.inputStream().use { it.copyTo(stream) } }
                            true
                        } catch (_: IOException) {
                            false
                        } catch (_: SecurityException) {
                            false
                        }
                    }
                    notice = when {
                        !ok -> "Could not write the .jambo file."
                        pack.missing > 0 -> "Project exported. ${missingMedia(pack.missing)}."
                        else -> "Project exported."
                    }
                } finally {
                    pack.file.delete()
                    busy = null
                }
            }
        } else {
            pack?.file?.delete()
            busy = null
        }
    }
    val openDocument = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            busy = "Importing project…"
            scope.launch {
                try {
                    val result = withContext(Dispatchers.IO + NonCancellable) { readProject(context, uri) }
                    if (result[0].isEmpty()) notice = result[1]
                    else {
                        imported = ImportedPack(result[0], result[1], result[2].toIntOrNull() ?: 0)
                        s.reload()
                    }
                } finally {
                    busy = null
                }
            }
        }
    }
    DisposableEffect(Unit) {
        onDispose { pending?.file?.delete() }
    }

    val enabled = busy == null
    MenuChoices(listOf("EXPORT" to false, "IMPORT" to false), enabled = listOf(enabled, enabled)) { index ->
        if (index == 1) {
            openDocument.launch(arrayOf("*/*"))
        } else {
            busy = "Preparing .jambo file…"
            scope.launch {
                var file: File? = null
                try {
                    val result = withContext(Dispatchers.IO) {
                        val temp = File.createTempFile("jambo-export-", ".jambo", context.cacheDir)
                        file = temp
                        AppNative.exportProjectPack(temp.path).takeIf { it >= 0 }?.let { ExportPack(temp, it) }
                    }
                    if (result == null) {
                        notice = "Could not prepare the .jambo file."
                    } else {
                        pending = result
                        busy = "Saving project…"
                        saveDocument.launch(exportName(name))
                    }
                } catch (_: IOException) {
                    notice = "Could not prepare the .jambo file."
                } catch (_: android.content.ActivityNotFoundException) {
                    pending = null
                    notice = "No system file picker is available."
                } finally {
                    if (pending == null) {
                        file?.delete()
                        busy = null
                    }
                }
            }
        }
    }
    busy?.let { MenuNote(it) }
    imported?.let { project ->
        val note = if (project.missing > 0) missingMedia(project.missing) + "." else null
        ChoiceDialog("Imported “${project.name}”", note, listOf(
            Triple("OK", true) { imported = null },
            Triple("LOAD", true) {
                imported = null
                s.project(ProjectAct.OPEN, project.id, "")
                close()
            },
        )) { imported = null }
    }
    notice?.let { text ->
        ChoiceDialog(text, null, listOf(Triple("OK", true) { notice = null })) { notice = null }
    }
}

private fun readProject(context: Context, uri: Uri): Array<String> {
    var file: File? = null
    return try {
        val temp = File.createTempFile("jambo-import-", ".jambo", context.cacheDir)
        file = temp
        val input = context.contentResolver.openInputStream(uri) ?: throw IOException()
        input.use { stream -> temp.outputStream().use { stream.copyTo(it) } }
        AppNative.importProjectPack(temp.path)
    } catch (_: IOException) {
        arrayOf("", "Could not read the .jambo file.")
    } catch (_: SecurityException) {
        arrayOf("", "Could not read the .jambo file.")
    } finally {
        file?.delete()
    }
}

private fun exportName(title: String): String = title.map { c ->
    if (c.isLetterOrDigit() || c == '-' || c == '_' || c == ' ') c else '_'
}.joinToString("").ifBlank { "project" } + ".jambo"

private fun missingMedia(count: Int): String =
    if (count == 1) "1 audio file unavailable" else "$count audio files unavailable"
