package app.jambo.android.ui

/** Ячейка сетки паттернов: биты (заполнена, играет, открыта, ожидает) и страницы миниатюры. */
class GridCell(val bits: Int, val pages: IntArray) {
    val filled get() = bits and 1 != 0
    val active get() = bits and 2 != 0
    val editing get() = bits and 4 != 0
    val queued get() = bits and 8 != 0
    /** Страниц в миниатюре; у страницы p — триги | хвосты << 16 и шаги за длиной. */
    val pageCount get() = pages.size / 2
}

/** Разбор grid() (jni_app.cpp). */
class GridData(a: IntArray) {
    val open = a.getOrElse(0) { 0 } != 0
    val tracks = a.getOrElse(1) { 0 }
    val top = a.getOrElse(2) { 0 }
    val maxTop = a.getOrElse(3) { 0 }
    val plusRow = a.getOrElse(4) { -1 }
    val windowRows = a.getOrElse(5) { 0 }
    val visibleRows = a.getOrElse(6) { 1 }
    val playing = a.getOrElse(7) { 0 } != 0
    val hasClipCopy = a.getOrElse(8) { 0 } != 0
    val hasRowCopy = a.getOrElse(9) { 0 } != 0
    val start = a.getOrElse(10) { 0 }
    val endStop = a.getOrElse(11) { 0 } != 0
    val plusCopy = a.getOrElse(12) { 0 } != 0
    val arrowSwitch = a.getOrElse(13) { 0 } != 0
    val rowLoop = a.getOrElse(14) { 0 } != 0
    val sceneRow = a.getOrElse(15) { -1 }
    val sceneQueued = a.getOrElse(16) { -1 }
    val canAddRow = a.getOrElse(17) { 0 } != 0
    /** Строка проекта у строки окна (−1 — «+» или пусто) и ячейки [строка окна][дорожка]. */
    val rows = IntArray(windowRows)
    val cells: Array<Array<GridCell>>
    val repeats = IntArray(windowRows)

    init {
        var i = 18
        cells = Array(windowRows) { r ->
            rows[r] = a.getOrElse(i++) { -1 }
            Array(tracks) {
                val bits = a.getOrElse(i++) { 0 }
                val n = a.getOrElse(i++) { 0 }
                val pg = IntArray(2 * n) { a.getOrElse(i + it) { 0 } }
                i += 2 * n
                GridCell(bits, pg)
            }
        }
        for (r in 0 until windowRows) repeats[r] = a.getOrElse(i++) { 1 }
    }
}
