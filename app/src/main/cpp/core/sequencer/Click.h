// Щелчок метронома (порт Core/Sequencer/Click.swift, ветка swift JAMbo): синус 1000 Гц (акцент — 1600), подъём за
// 2 мс, спад до −80 dB к 50 мс.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jambo {

class Click {
public:
    explicit Click(double sampleRate) : sampleRate(sampleRate) {}
    /// Громкость (0…2; 1 — пик 0,3, акцент 0,5).
    double gain = 1;

    /// Щелчок с отсчёта at (кадры ядра).
    void trigger(int64_t at, bool accent) {
        if (gain < 0.001) return;
        active = true;
        start = at;
        phaseInc = (accent ? 1600.0 : 1000.0) / sampleRate;
        peak = (accent ? 0.5 : 0.3) * gain;
    }

    /// Прибавить щелчок к блоку с кадра blockStart.
    void render(int64_t blockStart, int frames, float* left, float* right) {
        if (!active) return;
        const double attack = 0.002 * sampleRate;
        const double decay = 0.048 * sampleRate;
        const double stop = 0.06 * sampleRate;
        for (int i = 0; i < frames; ++i) {
            const double n = double(blockStart + i - start);
            if (n < 0) continue;
            if (n >= stop) {
                active = false;
                return;
            }
            const double g = n < attack ? 0.0001 * std::pow(peak / 0.0001, n / attack)
                                        : peak * std::pow(0.0001 / peak, std::min(1.0, (n - attack) / decay));
            const float s = float(std::sin(2 * M_PI * phaseInc * n) * g);
            left[i] += s;
            right[i] += s;
        }
    }

private:
    bool active = false;
    int64_t start = 0;
    double phaseInc = 0, peak = 0;
    double sampleRate;
};

}  // namespace jambo
