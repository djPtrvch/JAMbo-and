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
        const val SET_SOURCE = 85
        const val KNOB_FINGER = 86
        const val ASSIGN_SOURCE = 87
        const val ASSIGN_START = 88
        const val SET_ASSIGN = 89
        const val OPEN_SOUND_MENU = 90
        const val CLOSE_SOUND_MENU = 91
        const val PICK_SYNTH = 92
        const val CLEAN_SAMPLER = 93
        const val STEP_SOUND_TO_TRACK = 94
        const val FOLDER_ARROW = 95
        const val TICK = 100
        const val PAGE_PRESS = 120
        const val PAGE_RELEASE = 121
        const val SET_SHIFT = 122
        const val KNOB_SHIFT_TAP = 123
        const val SET_LATCHED = 124
        const val MORPH_PRESS = 125
        const val MORPH_RELEASE = 126
        const val OPEN_PAGE = 110
        const val ADD_PAGE = 111
        const val PLAY_PAGES = 112
        const val MOVE_PAGE = 113
        const val PAGE_ACTION = 114
        const val PAGE_ARROW = 115
        const val SET_CLIP_RATE = 116
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
    /** По слоту KNOB_STRIDE чисел: активна, положение, база, p-lock, от центра, 4-я строка, источник назначения
     *  (0 — нет), степень, ход источника от, до, держат этот источник. */
    external fun knobs(): FloatArray
    /** По слоту: подпись, значение. */
    external fun knobTexts(): Array<String>
    /** [число клавиш, число клавиш двух октав, по клавише: нота, биты подсвечена 1 | обведена 2 | корневая 4]. */
    external fun keys(): IntArray
    external fun keyLabels(): Array<String>
    /** [движок окна (−1 — сэмпл), звуков в папке, меню открыто, движок дорожки, сэмпл на дорожке, число шагов меню, шаги…]. */
    external fun sound(): IntArray
    /** [заголовок, после тире, пресет, подпись стрелок, строки…]. */
    external fun soundTexts(): Array<String>
    /** Колонки меню звука: [число DRUMS, номера…, число SYNTH, номера…]. */
    external fun engines(): IntArray
    /** Значения иконки окна звука: [движок (−1 — нет), h, t, m, q, d, am, ao]. */
    external fun iconValues(): FloatArray
    /** Элементы иконки (формат — icon в jni_app.cpp). */
    external fun icon(engine: Int, h: Float, t: Float, m: Float, q: Float, d: Float, am: Int, ao: Boolean): FloatArray
    /** Источники LFO1–3, ENV: [зажатые (бит 1 shl n), обведённые, есть назначения на ручках]. */
    external fun mod(): IntArray
    /** Положения ручек с модуляцией (−1 — без назначения) — каждый кадр, пока есть назначения. */
    external fun modNorms(): FloatArray
    /** Значения ручек с модуляцией (пусто — без назначения). */
    external fun modTexts(): Array<String>
    /** Миниатюры: [страниц, играют от, до, открыты от, до, скорость клипа, есть копия, по странице: duplicate, 16 ячеек]. */
    external fun pages(): IntArray
    external fun clipRates(): IntArray
    /** Кнопки правой колонки: [FX зажата, MOD зажата, FX toggle, MOD toggle, SHIFT, ручки FX, ручки MOD,
     *  SHIFT для ручек, MORPH горит, у MORPH линия MODE TAP]. */
    external fun side(): IntArray
    /** Миниатюры MOD: по LFO1–3 и ENV [точек n, x n, y n, точка x, y] подряд. */
    external fun modMinis(): FloatArray
    external fun modMiniTitles(): Array<String>
    external fun clipRateNames(): Array<String>
    /** Короткие имена движков по номеру. */
    external fun engineNames(): Array<String>
}

/** Действия страниц (App::PageAction). */
object PageAct {
    const val LEFT = 0
    const val RIGHT = 1
    const val UP = 2
    const val DOWN = 3
    const val NOTE_UP = 4
    const val NOTE_DOWN = 5
    const val DUPLICATE = 6
    const val COPY = 7
    const val PASTE = 8
    const val CLEAR = 9
    const val DELETE = 10
}

/** Чисел на ручку в knobs(). */
const val KNOB_STRIDE = 11

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
