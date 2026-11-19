package app.jambo.android.ui

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
