package app.jambo.android.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.jambo.android.AppNative

/**
 * Данные экрана из C++ (AppNative). Каждый кадр — frame (ход игры, мигание); разделы перечитываются, только когда
 * ревизия изменилась, и меняют своё состояние, только если содержимое другое: перерисовывается изменившееся.
 */
class ScreenState {
    var top by mutableStateOf(IntArray(6)); private set
    var topTexts by mutableStateOf(arrayOf("", "")); private set
    var tracks by mutableStateOf(IntArray(3)); private set
    var trackNames by mutableStateOf(emptyArray<String>()); private set
    var steps by mutableStateOf(IntArray(2)); private set
    var stepLabels by mutableStateOf(emptyArray<String>()); private set
    var knobs by mutableStateOf(FloatArray(16 * app.jambo.android.KNOB_STRIDE)); private set
    var mod by mutableStateOf(IntArray(3)); private set
    /** Модуляция ручек — каждый кадр, пока есть назначения. */
    var modNorms by mutableStateOf(FloatArray(16) { -1f }); private set
    var modTexts by mutableStateOf(Array(16) { "" }); private set
    var knobTexts by mutableStateOf(Array(32) { "" }); private set
    var keys by mutableStateOf(IntArray(2)); private set
    var keyLabels by mutableStateOf(emptyArray<String>()); private set
    var piano by mutableStateOf(IntArray(11)); private set
    var projects by mutableStateOf(IntArray(3)); private set
    var projectRows by mutableStateOf(arrayOf("")); private set
    var settings by mutableStateOf(IntArray(5)); private set
    var grid by mutableStateOf(IntArray(18)); private set
    var sceneNames by mutableStateOf(emptyArray<String>()); private set
    /** Ход сетки (каждый кадр, пока открыта) и фаза мигания ожидающих ячеек. */
    var gridLive by mutableStateOf(IntArray(2)); private set
    var blink by mutableStateOf(false); private set
    var eq by mutableStateOf(IntArray(5)); private set
    var eqBands by mutableStateOf(FloatArray(50)); private set
    var eqTexts by mutableStateOf(Array(7) { "" }); private set
    /** Поле EQ: столбцов спектра и точек кривой (задаёт экран), кадр спектров и кривых. */
    var eqCols = 0
    var eqSpec by mutableStateOf(FloatArray(0)); private set
    var eqCurve by mutableStateOf(FloatArray(1)); private set
    private var lastFrame = 0.0
    val eqUi = EqUi()
    /** Рамки кнопок верхней панели в координатах Body (строка EQ — по ним). */
    var topRects by mutableStateOf(mapOf<String, androidx.compose.ui.geometry.Rect>())
    val morphReturnNames: Array<String> by lazy { AppNative.morphReturnNames() }
    /** Меню бутерброда открыто. */
    var mainMenu by mutableStateOf(false)
    var pianoTexts by mutableStateOf(arrayOf("", "")); private set
    val scaleNames: Array<String> by lazy { AppNative.scaleNames() }
    val chordKindNames: Array<String> by lazy { AppNative.chordKindNames() }
    val rootNames: Array<String> by lazy { AppNative.rootNames() }
    /** Меню выбора (SCALE, ROOT), окно CHORD, меню клавиатуры. */
    var choice by mutableStateOf<Choice?>(null)
    var chordMenu by mutableStateOf(false)
    var pianoMenu by mutableStateOf(false)
    /** Координаты Body (меню поверх экрана) и рамки клавиш и строки дорожек в них (px). */
    var body: androidx.compose.ui.layout.LayoutCoordinates? = null
    var keysRect by mutableStateOf(androidx.compose.ui.geometry.Rect.Zero)
    var tracksRect by mutableStateOf(androidx.compose.ui.geometry.Rect.Zero)
    var sound by mutableStateOf(IntArray(6)); private set
    var pages by mutableStateOf(IntArray(7)); private set
    var side by mutableStateOf(IntArray(10)); private set
    /** Меню страницы: номер страницы (−1 — закрыто) и низ её миниатюры в колонке. */
    var pageMenu by mutableIntStateOf(-1)
    var pageMenuY by mutableStateOf(0f)
    val clipRates: IntArray by lazy { AppNative.clipRates() }
    val clipRateNames: Array<String> by lazy { AppNative.clipRateNames() }
    var iconValues by mutableStateOf(FloatArray(8) { -1f }); private set
    var soundTexts by mutableStateOf(arrayOf("", "", "", "")); private set
    /** Меню звука: колонки и имена движков (не меняются). */
    val engines: IntArray by lazy { AppNative.engines() }
    val engineNames: Array<String> by lazy { AppNative.engineNames() }

    var lib by mutableStateOf(IntArray(6)); private set
    var libRows by mutableStateOf(arrayOf("", "")); private set
    /** Форма волны окна сэмпла (столбцов — waveCols, задаёт окно). */
    var waveCols = 0
    var wave by mutableStateOf(FloatArray(1)); private set
    /** Полосы воспроизведения (каждый кадр, пока звучат голоса сэмплера). */
    var heads by mutableStateOf(FloatArray(0)); private set
    /** Мастер: bypass и точки скопа (по ревизии); индикация и скоп — каждый кадр при MIX и MST. */
    var masterInfo by mutableStateOf(IntArray(9)); private set
    var masterBars by mutableStateOf(FloatArray(21)); private set
    var scope by mutableStateOf(FloatArray(2)); private set
    var scopeWidth = 200
    var mixBars by mutableStateOf(FloatArray(1)); private set
    var mixStereo by mutableStateOf(FloatArray(0)); private set
    /** Вид обзора MIX: 0 LEVEL, 1 PAN, 2 FREQ, 3 MASK (MixMode, в C++). */
    val mixMode get() = masterInfo.getOrElse(8) { 0 }
    /** Идёт IMPORT (файлов осталось). */
    var importing by mutableIntStateOf(0)

    var playPos by mutableIntStateOf(-1); private set
    var flashMask by mutableIntStateOf(0); private set
    var beatFlash by mutableIntStateOf(0); private set
    var beatAccent by mutableIntStateOf(0); private set

    private var revision = -1

    /** Кадр экрана: ход игры из колбэка, затем данные по ревизии. */
    fun frame(nowSeconds: Double) {
        AppNative.act(AppNative.Act.TICK, 0, 0, nowSeconds)
        refresh()
        if (tracks[2] != 0) {
            scope = AppNative.scopeData(scopeWidth)
            if (tracks[2] == 2) masterBars = AppNative.masterBars(nowSeconds)
            if (tracks[2] == 1) {
                mixBars = AppNative.mixBars()
                mixStereo = AppNative.mixStereo()
            }
        }
        val dt = nowSeconds - lastFrame
        lastFrame = nowSeconds
        if (eq[0] != 0 && eqCols > 1) {
            eqSpec = AppNative.eqSpectrum(eqCols, dt)
            AppNative.eqCurves(eqCols).let { if (!it.contentEquals(eqCurve)) eqCurve = it }
        }
    }

    /** После действия — сразу (без ожидания кадра). */
    fun refresh() {
        val f = AppNative.frame()
        playPos = f[1]
        flashMask = f[2]
        beatFlash = f[3]
        beatAccent = f[4]
        if (f.getOrElse(6) { 0 } > 0 || heads.isNotEmpty()) AppNative.heads().let { if (!it.contentEquals(heads)) heads = it }
        if (mod[2] != 0) {
            AppNative.modNorms().let { if (!it.contentEquals(modNorms)) modNorms = it }
            AppNative.modTexts().let { if (!it.contentEquals(modTexts)) modTexts = it }
        }
        if (grid[0] != 0) {
            AppNative.gridLive().let { if (!it.contentEquals(gridLive)) gridLive = it }
            val b = (System.nanoTime() / 250_000_000L) % 2 == 0L
            if (b != blink) blink = b
        }
        if (f[0] == revision) return
        revision = f[0]
        AppNative.top().let { if (!it.contentEquals(top)) top = it }
        AppNative.topTexts().let { if (!it.contentEquals(topTexts)) topTexts = it }
        AppNative.tracks().let { if (!it.contentEquals(tracks)) tracks = it }
        AppNative.trackNames().let { if (!it.contentEquals(trackNames)) trackNames = it }
        AppNative.steps().let { if (!it.contentEquals(steps)) steps = it }
        AppNative.stepLabels().let { if (!it.contentEquals(stepLabels)) stepLabels = it }
        AppNative.knobs().let { if (!it.contentEquals(knobs)) knobs = it }
        AppNative.mod().let { if (!it.contentEquals(mod)) mod = it }
        AppNative.knobTexts().let { if (!it.contentEquals(knobTexts)) knobTexts = it }
        AppNative.keys().let { if (!it.contentEquals(keys)) keys = it }
        AppNative.keyLabels().let { if (!it.contentEquals(keyLabels)) keyLabels = it }
        AppNative.piano().let { if (!it.contentEquals(piano)) piano = it }
        AppNative.projects().let { if (!it.contentEquals(projects)) projects = it }
        AppNative.projectRows().let { if (!it.contentEquals(projectRows)) projectRows = it }
        AppNative.settings().let { if (!it.contentEquals(settings)) settings = it }
        AppNative.grid().let { if (!it.contentEquals(grid)) grid = it }
        AppNative.eq().let { if (!it.contentEquals(eq)) eq = it }
        AppNative.eqBands().let { if (!it.contentEquals(eqBands)) eqBands = it }
        AppNative.eqTexts().let { if (!it.contentEquals(eqTexts)) eqTexts = it }
        if (eq[0] != 0 && eqCols > 1) AppNative.eqCurves(eqCols).let { if (!it.contentEquals(eqCurve)) eqCurve = it }
        AppNative.sceneNames().let { if (!it.contentEquals(sceneNames)) sceneNames = it }
        AppNative.pianoTexts().let { if (!it.contentEquals(pianoTexts)) pianoTexts = it }
        AppNative.side().let { if (!it.contentEquals(side)) side = it }
        AppNative.pages().let { if (!it.contentEquals(pages)) pages = it }
        AppNative.sound().let { if (!it.contentEquals(sound)) sound = it }
        AppNative.iconValues().let { if (!it.contentEquals(iconValues)) iconValues = it }
        AppNative.soundTexts().let { if (!it.contentEquals(soundTexts)) soundTexts = it }
        AppNative.lib().let { if (!it.contentEquals(lib)) lib = it }
        AppNative.masterInfo().let { if (!it.contentEquals(masterInfo)) masterInfo = it }
        AppNative.libRows().let { if (!it.contentEquals(libRows)) libRows = it }
        if (waveCols > 0) AppNative.wave(waveCols).let { if (!it.contentEquals(wave)) wave = it }
    }

    /** Действие PROJECTS и сразу новые данные. */
    fun project(code: Int, id: String, text: String): String {
        val r = AppNative.projectAct(code, id, text)
        refresh()
        return r
    }

    /** Действие библиотеки и сразу новые данные. */
    fun libAct(code: Int, id: String = "", text: String = "") {
        AppNative.libAct(code, id, text)
        refresh()
    }

    /** Перечитать разделы (после IMPORT из фона). */
    fun reload() {
        revision = -1
        refresh()
    }

    /** Тема: 0 SYSTEM, 1 LIGHT, 2 DARK (настройки устройства). */
    var theme by mutableIntStateOf(0)
    /** Открытая вкладка SETTINGS. */
    var settingsTab by mutableIntStateOf(0)

    /** Действие и сразу новые данные. */
    fun act(code: Int, a: Int = 0, b: Int = 0, x: Double = 0.0): Double {
        val r = AppNative.act(code, a, b, x)
        refresh()
        return r
    }
}

/** Меню выбора: пункты, столбцы, выбранный, рамка поля (px в координатах Body), ширина (dp; null — 104 на столбец). */
class Choice(
    val options: Array<String>, val columns: Int, val current: Int, val anchor: androidx.compose.ui.geometry.Rect,
    val width: Float? = null, val pick: (Int) -> Unit,
)
