// Стерео по полосам и история громкости мастера при MIX (порт BandStereo.swift и LoudHistory.swift, ветка swift JAMbo).
// BandStereo: выход мастера (кольца гониометра) → LOW (до 150 Гц, LR4), MID (150 Гц…2 кГц), HIGH (выше 2 кГц, LR4);
// по полосе — сглаженные (~0,3 с) мощности L, R и L·R: корреляция и ширина (0 % — моно, 200 % — противофаза).
// LoudHistory: short-term (3 с) за последние 60 с игры, LRA (EBU Tech 3342), пик отсчётов — только во время игры.
#pragma once
#include <array>
#include <vector>
#include "engine/Levels.h"
#include "master/MasterScope.h"

namespace jambo {

class BandStereo {
public:
    static constexpr int cap = 4096;
    std::array<float, 3> corr{}, width{};
    std::array<bool, 3> live{};
    BandStereo();
    void tick(const MasterScope& s);
    void reset();

private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
        double z1 = 0, z2 = 0;
        float run(float x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return float(y);
        }
    };
    /// Фильтры полосы b канала c — f[b * 2 + c], по две секции.
    std::array<std::array<Biquad, 2>, 6> f;
    std::array<float, 3> ll{}, rr{}, lr{};
    long seen = -1;
    std::vector<float> inL = std::vector<float>(cap), inR = std::vector<float>(cap);
    void measure(int b);
};

class LoudHistory {
public:
    static constexpr int points = 300;
    static constexpr double step = 0.2;
    static constexpr int bins = 800;
    std::vector<float> values;
    int head = 0;
    float peak = 0;
    double lra = -1;
    LoudHistory();
    void tick(const Loudness& l, float framePeak, bool playing, double now);
    void reset();

private:
    std::vector<int> hist = std::vector<int>(bins, 0);
    double last = 0, lraAt = 0;
    double computeLra() const;
    double percentile(double gate, int kept, double q) const;
    static double level(int b) { return -70 + (double(b) + 0.5) / 10; }
};

}  // namespace jambo
