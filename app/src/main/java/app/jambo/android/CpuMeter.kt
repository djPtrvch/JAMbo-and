package app.jambo.android

import android.os.Process
import android.os.SystemClock

/** CPU процесса в % одного ядра — по времени CPU процесса (Process.getElapsedCpuTime). */
class CpuMeter {
    private var lastCpu = Process.getElapsedCpuTime()
    private var lastWall = SystemClock.elapsedRealtime()

    fun sample(): Double {
        val cpu = Process.getElapsedCpuTime()
        val wall = SystemClock.elapsedRealtime()
        val pct = if (wall > lastWall) 100.0 * (cpu - lastCpu) / (wall - lastWall) else 0.0
        lastCpu = cpu; lastWall = wall
        return pct
    }
}
