// Тест-тон (порт Core/Engine/TestTone.swift, ветка swift JAMbo): синус −12 dBFS, включение и выключение без щелчка (5 мс).
#pragma once
#include <cmath>

namespace jambo {

class TestTone {
public:
    static constexpr float level = 0.25118864f;
    explicit TestTone(double sampleRate, double frequency = 440)
        : sampleRate(sampleRate), increment(frequency / sampleRate),
          smooth(float(1 - std::exp(-1 / (0.005 * sampleRate)))) {}
    bool isOn() const { return target > 0; }
    void setOn(bool on) { target = on ? level : 0; }
    void setFrequency(double f) { increment = f / sampleRate; }
    /// Прибавить frames кадров к left и right.
    void render(int frames, float* left, float* right) {
        if (gain == 0 && target == 0) return;
        double p = phase;
        float g = gain;
        for (int i = 0; i < frames; ++i) {
            g += (target - g) * smooth;
            const float s = float(std::sin(2 * M_PI * p)) * g;
            left[i] += s;
            right[i] += s;
            p += increment;
            if (p >= 1) p -= 1;
        }
        if (target == 0 && g < 1e-6f) g = 0;
        phase = p;
        gain = g;
    }

private:
    double sampleRate;
    double phase = 0;
    double increment;
    float gain = 0, target = 0;
    float smooth;
};

}  // namespace jambo
