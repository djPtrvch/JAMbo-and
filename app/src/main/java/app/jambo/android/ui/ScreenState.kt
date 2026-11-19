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
    var top by mutableStateOf(IntArray(5)); private set
    var topTexts by mutableStateOf(arrayOf("", "")); private set
    var tracks by mutableStateOf(IntArray(3)); private set
    var trackNames by mutableStateOf(emptyArray<String>()); private set
    var steps by mutableStateOf(IntArray(2)); private set
    var stepLabels by mutableStateOf(emptyArray<String>()); private set
    var knobs by mutableStateOf(FloatArray(16 * 6)); private set
    var knobTexts by mutableStateOf(Array(32) { "" }); private set
    var keys by mutableStateOf(IntArray(1)); private set
    var keyLabels by mutableStateOf(emptyArray<String>()); private set

    var playPos by mutableIntStateOf(-1); private set
    var flashMask by mutableIntStateOf(0); private set
    var beatFlash by mutableIntStateOf(0); private set
    var beatAccent by mutableIntStateOf(0); private set

    private var revision = -1

    /** Кадр экрана: ход игры из колбэка, затем данные по ревизии. */
    fun frame(nowSeconds: Double) {
        AppNative.act(AppNative.Act.TICK, 0, 0, nowSeconds)
        refresh()
    }

    /** После действия — сразу (без ожидания кадра). */
    fun refresh() {
        val f = AppNative.frame()
        playPos = f[1]
        flashMask = f[2]
        beatFlash = f[3]
        beatAccent = f[4]
        if (f[0] == revision) return
        revision = f[0]
        AppNative.top().let { if (!it.contentEquals(top)) top = it }
        AppNative.topTexts().let { if (!it.contentEquals(topTexts)) topTexts = it }
        AppNative.tracks().let { if (!it.contentEquals(tracks)) tracks = it }
        AppNative.trackNames().let { if (!it.contentEquals(trackNames)) trackNames = it }
        AppNative.steps().let { if (!it.contentEquals(steps)) steps = it }
        AppNative.stepLabels().let { if (!it.contentEquals(stepLabels)) stepLabels = it }
        AppNative.knobs().let { if (!it.contentEquals(knobs)) knobs = it }
        AppNative.knobTexts().let { if (!it.contentEquals(knobTexts)) knobTexts = it }
        AppNative.keys().let { if (!it.contentEquals(keys)) keys = it }
        AppNative.keyLabels().let { if (!it.contentEquals(keyLabels)) keyLabels = it }
    }

    /** Действие и сразу новые данные. */
    fun act(code: Int, a: Int = 0, b: Int = 0, x: Double = 0.0): Double {
        val r = AppNative.act(code, a, b, x)
        refresh()
        return r
    }
}
