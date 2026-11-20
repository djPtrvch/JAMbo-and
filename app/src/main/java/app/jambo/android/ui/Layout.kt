package app.jambo.android.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Раскладка по ширине экрана (решение пользователя 21.11): левая колонка — ровно до правого края первой клавиши
 * клавиатуры, правая зеркально — от левого края последней клавиши (вместе с зазором и измерителем справа).
 * Клавиши — как при одной октаве (с двумя колонки не сужаются). Середина — сетка ручек и кнопки над ней — остаток; midStretch — её
 * растяжение против iPad.
 */
class Dims(val col: Float, val right: Float, val midStretch: Float) {
    companion object {
        /** Рабочая ширина iPad (без полей) и середина на нём. */
        const val ipadWidth = Layout.designWidth - 2 * Layout.side
        val ipadMid = ipadWidth - 2 * Layout.leftColumn - Layout.meterWidth - 3 * Layout.gap

        fun of(width: Float, keys: Int): Dims {
            val step = Layout.step(width)
            val area = width - 2 * step - 2 * Layout.stepGap
            val key = (area - Keyboard.GAP * (keys - 1)) / maxOf(1, keys)
            val edge = step + Layout.stepGap + key
            val right = maxOf(60f, edge - Layout.meterWidth - Layout.gap)
            val mid = width - edge - right - Layout.meterWidth - 3 * Layout.gap
            return Dims(edge, right, maxOf(0.5f, mid / ipadMid))
        }
    }
}

val LocalDims = staticCompositionLocalOf { Dims(Layout.leftColumn, Layout.leftColumn, 1f) }

/** Ширина кнопки середины (v точек iPad). */
@Composable
fun mid(v: Float): Dp = (v * LocalDims.current.midStretch).dp

/** Ширина левой колонки. */
@Composable
fun column(): Dp = LocalDims.current.col.dp

/** Ширина правой колонки (без измерителя). */
@Composable
fun rightColumn(): Dp = LocalDims.current.right.dp

/** Размеры раскладки (Layout.swift ветки swift JAMbo), dp. */
object Layout {
    const val gap = 6f
    const val radius = 10f
    const val stepGap = 4f
    const val topHeight = 44f
    const val trackHeight = 50f
    const val seqHeight = 60f
    const val topButton = 107f
    const val leftColumn = 220f
    const val meterWidth = 30f
    const val side = 12f
    /// Экран iPad Pro 11" в альбомной ориентации (точки): по нему масштабируется раскладка (MainScreen).
    const val designWidth = 1194f
    const val designHeight = 834f

    /** Ширина шага секвенсора (16 шагов с зазором 4) — и столбцов MIX и MST; width — без полей. */
    fun step(width: Float) = maxOf(1f, (width - 15 * stepGap) / 16)

    /** Сторона квадратной клавиши: ширина клавиши при числе клавиш count. */
    fun keySide(width: Float, count: Int): Float {
        val keys = width - 2 * step(width) - 2 * stepGap
        return maxOf(20f, (keys - 3 * (count - 1)) / count)
    }

    /** Высота клавиатуры: квадратная клавиша + 50 (место нижнего бутерброда). */
    fun pianoHeight(width: Float, count: Int) = keySide(width, count) + 50

    /** Высота CHORD (квадратная, шириной в шаг). */
    fun keyHeight(width: Float, count: Int) = maxOf(20f, minOf(step(width), pianoHeight(width, count) - 50))
}
