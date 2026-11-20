package app.jambo.android.ui

import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.sp

/** Цветовые токены (Theme.swift ветки swift JAMbo): тёмная и светлая темы. */
class Theme(
    val bg: Color, val panel: Color, val panel2: Color, val trackBg: Color, val flash: Color, val accentSoft: Color,
    val root: Color, val line: Color, val text: Color, val dim: Color, val off: Color, val accent: Color,
    val onAccent: Color, val rec: Color, val gr: Color, val meterTrack: Color,
    val accentText: Color, val icon: Color, val icon2: Color, val muteGrey: Color,
) {
    companion object {
        val dark = Theme(
            bg = Color(0xFF111111), panel = Color(0xFF1C1C1C), panel2 = Color(0xFF272727), trackBg = Color(0xFF2E2E2E),
            flash = Color(0xFF4A4A4A), accentSoft = Color(0xFF3A2F0A), root = Color(0xFF303030), line = Color(0xFF333333),
            text = Color(0xFFECECEC), dim = Color(0xFF8A8A8A), off = Color(0xFF3D3D3D), accent = Color(0xFFFFC81E),
            onAccent = Color(0xFF111111), rec = Color(0xFFFF2B2B), gr = Color(0xFFFF8A3D), meterTrack = Color(0xFF3A3A3A),
            accentText = Color(0xFFFFC81E), icon = Color(0xFFE8E8E8), icon2 = Color(0xFF6C6C72),
            muteGrey = Color(0xFF6B6B6B),
        )
        val light = Theme(
            bg = Color(0xFFE6E5E1), panel = Color(0xFFF7F6F3), panel2 = Color(0xFFFFFFFF), trackBg = Color(0xFFFFFFFF),
            flash = Color(0xFFD6D5D0), accentSoft = Color(0xFFFFEAA0), root = Color(0xFFE9E8E3), line = Color(0xFFCFCECA),
            text = Color(0xFF161616), dim = Color(0xFF77766F), off = Color(0xFFC4C3BE), accent = Color(0xFFF0B800),
            onAccent = Color(0xFF161616), rec = Color(0xFFE01418), gr = Color(0xFFF08A24), meterTrack = Color(0xFFCFCFCF),
            accentText = Color(0xFF8A6A00), icon = Color(0xFF161616), icon2 = Color(0xFF9B9AA0),
            muteGrey = Color(0xFFA3A29D),
        )
    }
}

val LocalTheme = staticCompositionLocalOf { Theme.dark }

/** Масштаб крупных подписей (имена дорожек, клавиши, ручки, верхняя панель): на телефоне — крупнее (решение
 *  пользователя 21.11), на планшете — 1 (проверить на устройстве). */
val LocalTextScale = staticCompositionLocalOf { 1f }

/** Размер шрифта с масштабом подписей. */
@androidx.compose.runtime.Composable
fun Number.tsp(): androidx.compose.ui.unit.TextUnit = (this.toFloat() * LocalTextScale.current).sp

/** Масштаб значков-символов (стрелки, луп, «+»): на телефоне — заметно крупнее подписей (решение пользователя 22.11). */
val LocalIconScale = staticCompositionLocalOf { 1f }

/** Размер символа-значка с масштабом значков. */
@androidx.compose.runtime.Composable
fun Number.isp(): androidx.compose.ui.unit.TextUnit = (this.toFloat() * LocalIconScale.current).sp

/** Масштаб подписей для рисования на холсте (вне композиции); ставит MainScreen. */
@Volatile var canvasTextScale = 1f
