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
        const val RESET_METER_PEAK = 130
        const val RESET_LOUDNESS = 131
        const val SET_METER_VOL = 132
        const val VOL_DRAG = 133
        const val TOGGLE_REC = 134
        const val OPEN_PAGE = 110
        const val ADD_PAGE = 111
        const val PLAY_PAGES = 112
        const val MOVE_PAGE = 113
        const val PAGE_ACTION = 114
        const val PAGE_ARROW = 115
        const val SET_CLIP_RATE = 116
        const val SET_SCALE = 140
        const val SET_ROOT = 141
        const val TOGGLE_CHORD = 142
        const val SET_CHORD_KIND = 143
        const val SET_PIANO_SETTINGS = 144
        const val SET_RECORD_SETTINGS = 145
        const val SET_SWING = 146
        const val FLUSH_AUTOSAVE = 150
        const val REFRESH_PROJECTS = 151
        const val SET_MORPH_SETTINGS = 152
        const val SET_LEVEL_RELATIVE = 153
        const val TOGGLE_PATTERNS = 160
        const val TAP_CELL = 161
        const val HOLD_CELL = 162
        const val QUEUE_SCENE = 163
        const val COPY_CLIP = 164
        const val PASTE_CLIP = 165
        const val DELETE_CLIP = 166
        const val DROP_CLIP = 167
        const val ADD_ROW = 168
        const val DUPLICATE_ROW = 169
        const val MOVE_ROW = 170
        const val DELETE_ROW = 171
        const val COPY_ROW = 172
        const val PASTE_ROW = 173
        const val SET_GRID_OPTION = 174
        const val GRID_ARROW = 175
        const val SET_GRID_TOP = 176
        const val TOGGLE_EQ_VIEW = 180
        const val SELECT_EQ_BAND = 181
        const val SET_EQ_TYPE = 182
        const val SET_EQ_SLOPE = 183
        const val SET_EQ_MS = 184
        const val SET_EQ_DYN = 185
        const val TOGGLE_EQ_BAND = 186
        const val DELETE_EQ_BAND = 187
        const val TOGGLE_EQ_BYPASS = 188
    }

    /** EQ: [открыт, мастер, включён, выбранная полоса, дорожек], по полосе (10): [есть, вкл., тип, крутизна, M/S, DYN]. */
    external fun eq(): IntArray
    /** По полосе: [частота, усиление, Q, порог, сдвиг]. */
    external fun eqBands(): FloatArray
    /** [имя канала, TYPE, FREQ, GAIN, Q, M/S, DYN]. */
    external fun eqTexts(): Array<String>
    /** Правка полосы: NaN — не менять. */
    external fun eqSetBand(k: Int, freq: Double, gain: Double, q: Double)
    external fun eqAddBand(freq: Double, gain: Double)
    /** THRESHOLD и RANGE динамической полосы: NaN — не менять. */
    external fun eqSetDyn(k: Int, thr: Double, range: Double)
    /** [биты стерео 1 | MID 2 | SIDE 4, стерео n, середина n, бока n, сдвиги DYN 10] — dB. */
    external fun eqCurves(n: Int): FloatArray
    /** [до EQ n, после EQ n] — доли высоты. */
    external fun eqSpectrum(n: Int, dt: Double): FloatArray

    /** Сетка паттернов (формат — grid в jni_app.cpp). */
    external fun grid(): IntArray
    /** Имена строк (без имени — номер), затем сами имена (пустые — без имени). */
    external fun sceneNames(): Array<String>
    /** [проход строки, доля·1000, по дорожке доля играющей ячейки·1000] — каждый кадр. */
    external fun gridLive(): IntArray
    external fun setScene(k: Int, name: String, repeats: Int)

    /** Папка проектов: открыть Autosave, иначе последний проект. */
    external fun openStorage(dir: String)
    /** [не сохранено, новый, проектов в списке]. */
    external fun projects(): IntArray
    /** По 6 на проект: id, подпись, время (с), темп, тональность, признаки (a — Autosave, c — текущий); в конце имя
     *  текущего проекта. */
    external fun projectRows(): Array<String>
    /** Действие PROJECTS (ProjectAct) со строками; ответ — у NAMED (id) и RANDOM_NAME. */
    external fun projectAct(code: Int, id: String, text: String): String
    /** [QUANTIZE, OVERDUB, MORPH RETURN, MORPH HOLD, LEVEL RELATIVE]. */
    external fun settings(): IntArray
    external fun morphReturnNames(): Array<String>

    external fun act(code: Int, a: Int, b: Int, x: Double): Double

    /** Библиотека (jni_lib.cpp): [открыт, в корне, выбор нескольких, отмечено, шагов загрузки, строк]. */
    external fun lib(): IntArray
    /** [путь, подпись внизу, по строке: id, вид (f, s, p), имя, справа, звук дорожки, отмечен]. */
    external fun libRows(): Array<String>
    external fun libAct(code: Int, id: String, text: String)
    external fun libImportFolder(): String
    /** SAVE: 1 — имя по умолчанию, 2 — id узла с этим именем (пусто — свободно), 3 — свободное имя. */
    external fun libText(code: Int, text: String): String
    /** IMPORT (поток вне UI): 1 — сэмпл, 3 — пресет, 2 — декодировать экраном (libImportPcm), 0 — нет. */
    external fun libImportBytes(name: String, bytes: ByteArray, folder: String): Int
    external fun libImportPcm(name: String, key: String, pcm: FloatArray, channels: Int, rate: Int, folder: String): Int
    external fun libNote(text: String)
    /** Форма волны окна: [каналов (0 — не сэмпл), Start, End, по каналу cols пиков по модулю]. */
    external fun wave(cols: Int): FloatArray
    /** Полосы воспроизведения окна сэмпла: пары доля, непрозрачность. */
    external fun heads(): FloatArray

    /** Мастер (jni_master.cpp): [bypass SAT, COMP, CLIP, LIM, точки скопа SAT, COMP, CLIP, LIM]. */
    external fun masterInfo(): IntArray
    /** code 1 — кнопка модуля k (с SHIFT — BYPASS), 2 — точка скопа k, 3 — вид полос MIX. */
    external fun masterAct(code: Int, k: Int)
    /** По блоку COMP, CLIP, LIM: [IN dB, OUT dB, GR dB, IN красный, OUT красный, bypass, риска dB]. */
    external fun masterBars(now: Double): FloatArray
    /** [корреляция (−9 — тишина), n, n пар side, mid, по точке 0…3: w, w мин., w макс.]. */
    external fun scopeData(width: Int): FloatArray
    /** Обзор MIX: [n, по дорожке (73): L, R, холд L, холд R (доли), перегруз, слышна, линейные L, R, LOUD, FREQ низ,
     *  верх, пик (−1 — тишина), мешающая, 30 сил MASK, 30 «заглушают»]. */
    external fun mixBars(): FloatArray
    /** Справа при MIX: [3 × (сигнал, корреляция, ширина %), short-term, интегральная, PLR, LRA, 300 short-term]. */
    external fun mixStereo(): FloatArray

    /** Окно модуля мастера (jni_masterwin.cpp): [модуль (−1 — нет), UNITY, BYPASS, полей, пресетов, SAVE DEFAULT мигает,
     *  FREEZE, окно микшера]. */
    external fun masterWin(): IntArray
    /** [по полю id, подпись, значение], затем имена пресетов. */
    external fun masterWinTexts(): Array<String>
    external fun masterWinAct(code: Int, k: Int, k2: Int, text: String)
    external fun masterPresetIndex(name: String, except: Int): Int
    external fun masterPresetNextName(): String
    /** code 1 — положение 0…1, 2 — значение, 3 — задать значение x, 4 — задать положение x, 5 — тап. */
    external fun masterParam(id: String, code: Int, x: Double): Double
    /** text пусто — текст поля для ввода; иначе разбор ("1" — принято). */
    external fun masterInput(id: String, text: String): String
    /** Данные дисплея окна (см. jni_masterwin.cpp). */
    external fun masterDisplay(cols: Int, tn: Int): FloatArray
    /** Окно микшера: [n, по дорожке (10): стерео, заглушена, L, холд L, R, холд R, Sidechain dB, клиппинг, пик, громкость dB]. */
    external fun mixerData(now: Double): FloatArray
    external fun mixerResetPeak(ch: Int)
    /** Микшер: [слот меню (−1), ADD, есть копия, можно удалить, n, по дорожке mute, solo, ●, точка; MASTER ●, точка]. */
    external fun mixInfo(): IntArray
    /** 1 mute, 2 solo, 3 ● (−1 — MASTER), 4 меню, 5 закрыть, 6 ADD, 7 COPY, 8 PASTE, 9 DELETE, 10 RENAME, 11 a → b. */
    external fun mixAct(code: Int, a: Int, b: Int, text: String)
    fun act(code: Int, a: Int = 0, b: Int = 0) = act(code, a, b, 0.0)

    /** [ревизия, ход открытого клипа, маска мигания, мигание доли, акцент, игра]. */
    external fun frame(): IntArray
    /** [BPM·10, UNDO, REDO, игра, метроном, REC]. */
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
    /** [CHORD вкл., тип аккорда, октав, VELOCITY, OVERDUB, гамма, корневая, SWING·10, SWING от·10, до·10]. */
    external fun piano(): IntArray
    /** [тип аккорда, SWING]. */
    external fun pianoTexts(): Array<String>
    external fun scaleNames(): Array<String>
    external fun chordKindNames(): Array<String>
    external fun rootNames(): Array<String>
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
    /** Измеритель: [канал, стерео, заглушён, полос n, по полосе уровень и холд, клиппинг, LUFS, пик, громкость, палец]. */
    external fun meter(now: Double): FloatArray
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

/** Действия библиотеки (libAct в jni_lib.cpp). */
object LibAct {
    const val OPEN = 1
    const val CLOSE = 2
    const val ENTER = 3
    const val UP = 4
    const val TAP = 5
    const val HOLD = 6
    const val PICK_ALL = 7
    const val CANCEL_PICK = 8
    const val LOAD_PICKED = 9
    const val PREVIEW = 10
    const val NEW_FOLDER = 11
    const val RENAME = 12
    const val DELETE = 13
    const val SAVE = 14
    const val OPEN_SAVE = 15
}

/** Действия окна модуля мастера (masterWinAct). */
object MasterWinAct {
    const val OPEN = 1
    const val CLOSE = 2
    const val UNITY = 3
    const val PRESET_SAVE = 4
    const val PRESET_LOAD = 5
    const val PRESET_RENAME = 6
    const val PRESET_DELETE = 7
    const val PRESET_MOVE = 8
    const val SAVE_DEFAULT = 9
    const val FREEZE = 10
    const val MIXER_OPEN = 11
    const val MIXER_CLOSE = 12
}

/** Действия PROJECTS (projectAct в jni_app.cpp). */
object ProjectAct {
    const val NEW = 0
    const val SAVE = 1
    const val SAVE_AS = 2
    const val RENAME = 3
    const val DELETE = 4
    const val OPEN = 5
    const val NAMED = 6
    const val RANDOM_NAME = 7
}

/** Настройки сетки (App::GridOption). */
object GridOpt {
    const val START = 0
    const val END_STOP = 1
    const val PLUS_COPY = 2
    const val ARROW_SWITCH = 3
    const val ROW_LOOP = 4
}
