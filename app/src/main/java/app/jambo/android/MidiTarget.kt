package app.jambo.android

/** Коды сохранённых целей. Соответствуют MidiTarget.h на стороне Core. */
object MidiTarget {
    const val PRESS_PARAMETER = -2
    const val KNOBS = 16
    const val PRESS = 16
    const val PLAY = 32
    const val REC = 33
    const val UNDO = 34
    const val REDO = 35
    const val METRONOME = 36
    const val SHIFT = 37
    const val FX = 38
    const val MOD = 39
    const val EQ = 40
    const val PATTERNS = 41
    const val STEP = 48
    const val BPM = 64
    const val LEVEL = 65
    const val CHORD = 66
    const val OCTAVE_UP = 67
    const val OCTAVE_DOWN = 68
    const val LFO = 69
    const val MORPH = 73
    const val MIX = 74
    const val MASTER = 75
    const val ARM_MASTER = 76
    const val GRID_UP = 77
    const val GRID_DOWN = 78
    const val SOUND_LEFT = 79
    const val SOUND_RIGHT = 80
    const val PAGE_ARROW = 81
    const val ARP = 85
    const val TRACK = 100
    const val MUTE = 116
    const val SOLO = 132
    const val ARM = 148
    const val KEY = 164
    const val CHORD_TYPE = 200
    const val ROW = 240
    const val PAGE = 248
    const val CLIP = 300
    const val WINDOW_ROWS = 8

    fun clip(track: Int, row: Int) = CLIP + track * WINDOW_ROWS + row
    fun continuous(target: Int) = target in 0 until KNOBS || target == BPM || target == LEVEL
    fun launcher(target: Int) =
        target == GRID_UP || target == GRID_DOWN || target in ROW until ROW + WINDOW_ROWS ||
            target in CLIP until CLIP + 16 * WINDOW_ROWS

    fun actions(chords: Int) = (PLAY..PATTERNS) + (CHORD..ARP) +
        (TRACK until KEY + 25) + (CHORD_TYPE until CHORD_TYPE + chords) + (ROW until PAGE + 8) + (STEP until STEP + 16)
}

fun midiTargetLabel(target: Int): String = with(MidiTarget) {
    when (target) {
        in 0 until KNOBS -> "KNOB ${target / 4 + 1}.${target % 4 + 1}"
        in PRESS until PLAY -> midiTargetLabel(target - PRESS) + " PRESS"
        in PLAY..PATTERNS -> listOf("PLAY", "REC", "UNDO", "REDO", "METRONOME", "SHIFT", "FX", "MOD", "EQ", "PATTERNS")[target - PLAY]
        in STEP until STEP + 16 -> "STEP ${target - STEP + 1}"
        in BPM..PAGE_ARROW + 3 -> listOf("BPM", "LEVEL", "CHORD", "OCTAVE ▲", "OCTAVE ▼", "LFO1", "LFO2", "LFO3", "ENV",
            "MORPH", "MIX", "MST", "ARM MST", "GRID ▲", "GRID ▼", "SOUND ◀", "SOUND ▶", "PAGE ◀", "PAGE ▶", "PAGE ▲", "PAGE ▼")[target - BPM]
        ARP -> "ARP"
        in TRACK until MUTE -> "T${target - TRACK + 1}"
        in MUTE until SOLO -> "MUTE ${target - MUTE + 1}"
        in SOLO until ARM -> "SOLO ${target - SOLO + 1}"
        in ARM until KEY -> "ARM ${target - ARM + 1}"
        in KEY until KEY + 25 -> "KEY ${target - KEY + 1}"
        in CHORD_TYPE until ROW -> "CHORD TYPE ${target - CHORD_TYPE + 1}"
        in ROW until PAGE -> "ROW ${target - ROW + 1}"
        in PAGE until PAGE + 8 -> "PAGE ${target - PAGE + 1}"
        in CLIP until CLIP + 16 * WINDOW_ROWS ->
            "CLIP ${(target - CLIP) / WINDOW_ROWS + 1}.${(target - CLIP) % WINDOW_ROWS + 1}"
        else -> ""
    }
}
