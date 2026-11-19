package app.jambo.android

/** Мост к состоянию приложения в C++ (app/App, jni_app.cpp): действия — act, данные для экрана — по разделам. */
object AppNative {
    init { System.loadLibrary("jambo") }

    /** Коды действий — enum Act в jni_app.cpp. */
    object Act {
        const val TOGGLE_PLAY = 1
        const val TOGGLE_METRONOME = 2
        const val UNDO = 3
        const val REDO = 4
        const val SET_BPM = 5
        const val BEGIN_GESTURE = 6
        const val END_GESTURE = 7
        const val SELECT_TRACK = 20
        const val SELECT_CHANNEL = 21
        const val SWIPE = 22
        const val HOLD_STEP = 40
        const val TOGGLE_STEP = 41
        const val SET_NOTE_LENGTH = 42
        const val COPY_STEP = 43
        const val RESTORE_COPY = 44
        const val SET_PATTERN_LENGTH = 45
        const val STEP_USED = 46
        const val KNOB_START = 60
        const val SET_KNOB = 61
        const val KNOB_DOUBLE_TAP = 62
        const val KEY_DOWN = 80
        const val KEY_UP = 81
        const val OCTAVE_STEP = 82
        const val MIDI_NOTE = 83
        const val TICK = 100
    }

    external fun act(code: Int, a: Int, b: Int, x: Double): Double
    fun act(code: Int, a: Int = 0, b: Int = 0) = act(code, a, b, 0.0)

    /** [ревизия, ход открытого клипа, маска мигания, мигание доли, акцент, игра]. */
    external fun frame(): IntArray
    /** [BPM·10, UNDO, REDO, игра, метроном]. */
    external fun top(): IntArray
    /** [гамма, корневая нота]. */
    external fun topTexts(): Array<String>
    /** [число, выбранная, канал, по дорожке: mute 1 | solo 2]. */
    external fun tracks(): IntArray
    external fun trackNames(): Array<String>
    /** [первая страница, последняя, по шагу — биты StepBits]. */
    external fun steps(): IntArray
    external fun stepLabels(): Array<String>
    /** По слоту 6 чисел: активна, положение, база, p-lock, от центра, 4-я строка. */
    external fun knobs(): FloatArray
    /** По слоту: подпись, значение. */
    external fun knobTexts(): Array<String>
    /** [число клавиш, по клавише: нота, биты подсвечена 1 | обведена 2 | корневая 4]. */
    external fun keys(): IntArray
    external fun keyLabels(): Array<String>
}

/** Биты шага (jni_app.cpp, steps). */
object StepBits {
    const val ON = 1
    const val TAIL = 2
    const val HELD = 4
    const val OUTSIDE = 8
    const val LAST = 16
    const val MANUAL_LOCK = 32
    const val COL4_LOCK = 64
    const val OWN_SOUND = 128
}
