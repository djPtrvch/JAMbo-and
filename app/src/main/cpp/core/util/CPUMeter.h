// Нагрузка аудиоколбэка: время расчёта блока / длительность блока, среднее за ~0,5 с звука (порт
// Core/Util/CPUMeter.swift, ветка swift JAMbo). measure — в колбэке (только арифметика); load — UI.
#pragma once
#include <algorithm>
#include "AtomicFloat.h"

namespace jambo {

class CPUMeter {
public:
    explicit CPUMeter(double sampleRate, double windowSeconds = 0.5)
        : window(int(sampleRate * windowSeconds)), sampleRate(sampleRate) {}

    /// Блок из blockFrames кадров считался ns наносекунд.
    void measure(double ns, int blockFrames) {
        busyNs += ns;
        frames += blockFrames;
        const double blockNs = double(blockFrames) / sampleRate * 1e9;
        if (blockNs > 0) peakNow = std::max(peakNow, ns / blockNs);
        if (frames >= window) {
            const double wallNs = double(frames) / sampleRate * 1e9;
            value.store(float(busyNs / wallNs));
            peakValue.store(float(peakNow));
            busyNs = 0;
            frames = 0;
            peakNow = 0;
        }
    }

    /// Средняя нагрузка 0…1 (доля одного ядра).
    float load() const { return value.load(); }
    /// Наибольшая нагрузка одного блока за окно 0…1.
    float peak() const { return peakValue.load(); }

private:
    int window;
    double sampleRate;
    double busyNs = 0;
    int frames = 0;
    AtomicFloat value{0};
    AtomicFloat peakValue{0};
    double peakNow = 0;
};

}  // namespace jambo
