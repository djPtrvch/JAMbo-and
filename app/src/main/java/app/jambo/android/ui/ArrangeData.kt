package app.jambo.android.ui

import androidx.compose.ui.graphics.Color
import app.jambo.android.AppNative

internal object ArrangeAct {
    const val TOGGLE = 0
    const val SEEK = 1
    const val SELECT = 2
    const val ADD = 3
    const val INSERT = 4
    const val MOVE = 5
    const val RESIZE = 6
    const val DUPLICATE = 7
    const val REMOVE = 8
    const val SPLIT = 9
    const val GRID = 10
    const val SNAP = 11
    const val LOOP = 12
    const val LOOP_RANGE = 13
    const val STEPS = 14
    const val AUTO = 15
    const val ROW = 16
    const val COLOR = 17
    const val SECTION = 18
    const val SECTION_START = 19
    const val SECTION_END = 20
    const val DELETE_SECTION = 21
}

internal fun ScreenState.arrangeAct(command: Int, a: Int = 0, b: Int = 0, c: Int = 0, d: Int = 0) {
    AppNative.arrangeAct(command, a, b, c, d)
    refresh()
}

internal data class ArrangeBlock(val start: Int, val length: Int, val clip: Int, val offset: Int) {
    val end get() = start + length
}

internal data class ArrangeTrack(val color: Int, val mute: Boolean, val solo: Boolean, val blocks: List<ArrangeBlock>)
internal data class ArrangeSection(val start: Int, val end: Int, val color: Int)

internal class ArrangeData(raw: IntArray) {
    val grid = raw[1]
    val snap = raw[2] != 0
    val hideSteps = raw[3] != 0
    val auto = raw[4] != 0
    val loop = raw[5] != 0
    val loopStart = raw[6]
    val loopEnd = raw[7]
    val end = raw[8]
    val selectedTrack = raw[9]
    val selectedBlock = raw[10]
    val sections: List<ArrangeSection>
    val tracks: List<ArrangeTrack>

    init {
        var i = 13
        fun read() = raw[i++]
        sections = List(raw[12]) { ArrangeSection(read(), read(), read()) }
        tracks = List(raw[11]) {
            ArrangeTrack(read(), read() != 0, read() != 0,
                List(read()) { ArrangeBlock(read(), read(), read(), read()) })
        }
    }

    val gridSteps get() = listOf(16, 8, 4, 1)[grid.coerceIn(0, 3)]
}

internal val arrangeColors = listOf(
    0x464649, 0x5E6166, 0x8E9298, 0xB8902A, 0xD0D3D7, 0xFAF0A8, 0xF0D55A, 0xCDA830, 0xA67C1A, 0xF5964A,
    0xBF5F24, 0x733519, 0xEC746E, 0xDC5468, 0xCF4747, 0xA33048, 0xE0DCD4, 0xB5B0A8, 0x7D2A2A, 0x6A2430,
).map { Color(it or 0xff000000.toInt()) }

internal fun arrangeColor(index: Int) = arrangeColors[index.coerceIn(arrangeColors.indices)]
