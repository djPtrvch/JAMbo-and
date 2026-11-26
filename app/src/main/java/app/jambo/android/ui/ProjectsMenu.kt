package app.jambo.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.jambo.android.ProjectAct
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Что вводят в окне ввода: имя копии (SAVE AS), имя нового проекта при первом SAVE, новое имя проекта из списка. */
private sealed class ProjectInput {
    object SaveAs : ProjectInput()
    object SaveNew : ProjectInput()
    class Rename(val id: String) : ProjectInput()
}

/** Строка списка проектов (projectRows). */
private class ProjectRow(val id: String, val title: String, val updated: Long, val bpm: String, val key: String,
                         val autosave: Boolean, val current: Boolean)

private const val NAME_LIMIT = 24

/** Меню PROJECTS (ProjectsMenu.swift): «имя · unsaved»; NEW, SAVE, SAVE AS; EXPORT, IMPORT .jambo; список: тап —
 *  окно открытия (LOAD; MIX и IMPORT строк — позже), свайп влево — EDIT и корзина (у Autosave — только корзина).
 *  SAVE AS с именем существующего проекта спрашивает, перезаписать ли его. */
@Composable
fun ProjectsPage(s: ScreenState, back: () -> Unit, close: () -> Unit) {
    val r = s.projectRows
    val name = r.lastOrNull() ?: ""
    val unsaved = s.projects[0] != 0
    var input by remember { mutableStateOf<Pair<ProjectInput, String>?>(null) }
    var overwrite by remember { mutableStateOf<Pair<String, String>?>(null) }  // id, имя
    var opening by remember { mutableStateOf<ProjectRow?>(null) }
    var openRow by remember { mutableStateOf<String?>(null) }
    val rows = (0 until (r.size - 1) / 6).map { i ->
        val k = i * 6
        ProjectRow(r[k], r[k + 1], r[k + 2].toLongOrNull() ?: 0, r[k + 3], r[k + 4], r[k + 5] == "a", r[k + 5] == "c")
    }
    val act = { code: Int, id: String, text: String -> s.project(code, id, text) }
    Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        MenuBackHeader(if (unsaved) "$name · unsaved" else name, back)
        MenuChoices(listOf("NEW" to false, "SAVE" to false, "SAVE AS" to false)) { i ->
            when (i) {
                0 -> { close(); act(ProjectAct.NEW, "", "") }
                1 -> if (s.projects[1] != 0) input = ProjectInput.SaveNew to act(ProjectAct.RANDOM_NAME, "", "")
                    else act(ProjectAct.SAVE, "", "")
                else -> input = ProjectInput.SaveAs to "$name copy"
            }
            Unit
        }
        ProjectFileButtons(s, name, close)
        MenuHead("PROJECTS")
        if (rows.isEmpty()) MenuNote("No saved projects yet — save one with SAVE")
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            for (p in rows) {
                SwipeRow(
                    title = p.title, subtitle = date(p.updated), meta = "${p.bpm} · ${p.key}",
                    tint = if (p.autosave) LocalTheme.current.accentText else null, current = p.current,
                    open = openRow == p.id, setOpen = { openRow = if (it) p.id else null },
                    tap = { opening = p },
                    rename = if (p.autosave) null else ({ input = ProjectInput.Rename(p.id) to p.title }),
                    delete = { act(ProjectAct.DELETE, p.id, "") },
                )
            }
        }
    }
    input?.let { (what, initial) ->
        val title = when (what) { ProjectInput.SaveAs -> "SAVE AS"; ProjectInput.SaveNew -> "SAVE"; is ProjectInput.Rename -> "RENAME" }
        InputDialog(title, initial, NAME_LIMIT, cancel = { input = null }) { text ->
            input = null
            when (what) {
                is ProjectInput.Rename -> act(ProjectAct.RENAME, what.id, text)
                else -> {
                    val same = act(ProjectAct.NAMED, "", text)
                    if (same.isNotEmpty()) overwrite = same to text.trim() else act(ProjectAct.SAVE_AS, "", text)
                }
            }
            Unit
        }
    }
    overwrite?.let { (id, n) ->
        ChoiceDialog("“$n” already exists. Overwrite it?", null, listOf(
            Triple("CANCEL", true) { overwrite = null },
            Triple("OVERWRITE", true) { overwrite = null; act(ProjectAct.SAVE_AS, id, n) },
        )) { overwrite = null }
    }
    opening?.let { p ->
        val text = if (p.current && unsaved) "Open saved “${p.title}”? Unsaved changes will be lost." else "Open “${p.title}”"
        ChoiceDialog(text, null, listOf(
            Triple("LOAD", true) { opening = null; close(); act(ProjectAct.OPEN, p.id, "") },
            Triple("MIX", false) { },
            Triple("IMPORT ROWS", false) { },
        )) { opening = null }
    }
}

private val dateFormat = SimpleDateFormat("dd MMM, HH:mm", Locale.UK)

private fun date(t: Long): String = dateFormat.format(Date(t * 1000))
