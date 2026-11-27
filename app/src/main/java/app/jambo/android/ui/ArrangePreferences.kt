package app.jambo.android.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.platform.LocalContext
import app.jambo.android.AppNative

internal data class ArrangePreferences(
    val scaleTracks: Boolean = false,
    val compact: Boolean = false,
    val follow: Boolean = true,
    val rowRepeats: Boolean = true,
    val recordFromStart: Boolean = false,
    val stretchBeat: Boolean = false,
    val duckSends: Int = 4,
    val clickLevel: Int = 100,
    val drumPaint: Boolean = true,
    val knobColumns: Boolean = false,
)

@Composable
internal fun LoadArrangePreferences(s: ScreenState) {
    val context = LocalContext.current
    LaunchedEffect(s, context) {
        val prefs = context.getSharedPreferences("jambo", 0)
        s.arrangePreferences = ArrangePreferences(
            scaleTracks = prefs.getBoolean("arrangeScale", false),
            compact = prefs.getBoolean("arrangeCompact", false),
            follow = prefs.getBoolean("arrangeFollow", true),
            rowRepeats = prefs.getBoolean("arrangeRepeats", true),
            recordFromStart = prefs.getBoolean("arrangeRecordFromStart", false),
            stretchBeat = prefs.getBoolean("stretchBeat", false),
            duckSends = prefs.getInt("duckSends", 4) and 7,
            clickLevel = prefs.getInt("clickLevel", 100).coerceIn(0, 200),
            drumPaint = prefs.getBoolean("drumPaint", true),
            knobColumns = prefs.getBoolean("knobColumns", false),
        )
        snapshotFlow { s.arrangePreferences }.collect { current ->
            s.applySoundPreferences(current)
            prefs.edit().putBoolean("arrangeScale", current.scaleTracks)
                .putBoolean("arrangeCompact", current.compact).putBoolean("arrangeFollow", current.follow)
                .putBoolean("arrangeRepeats", current.rowRepeats).putBoolean("arrangeRecordFromStart", current.recordFromStart)
                .putBoolean("stretchBeat", current.stretchBeat).putInt("duckSends", current.duckSends)
                .putInt("clickLevel", current.clickLevel).putBoolean("drumPaint", current.drumPaint)
                .putBoolean("knobColumns", current.knobColumns).apply()
        }
    }
}

private fun ScreenState.applySoundPreferences(p: ArrangePreferences) {
    AppNative.setAdditionalSettings(p.rowRepeats, p.recordFromStart, p.stretchBeat, p.duckSends, p.clickLevel)
    refresh()
}
