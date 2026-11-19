package app.jambo.android

/** Мост к C++ (libjambo.so): звук Oboe и Ableton Link. */
object Native {
    init { System.loadLibrary("jambo") }

    external fun start(): Boolean
    external fun stop()
    external fun setTone(on: Boolean)
    external fun tap(touchNanos: Long)
    external fun noteOn(note: Int, velocity: Int)
    external fun noteOff(note: Int)
    /** Порядок значений — AudioEngine::info (audio_engine.cpp). */
    external fun audioInfo(): DoubleArray

    external fun linkEnable(on: Boolean)
    external fun linkSetTempo(bpm: Double)
    /** [включён, участники, темп, доля, фаза]. */
    external fun linkInfo(outLatencyUs: Long): DoubleArray
}
