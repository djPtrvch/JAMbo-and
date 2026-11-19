// Детекторы динамических полос EQ (порт Core/EQ/EqDyn.swift, ветка swift JAMbo): источник → фильтр детектора (у bell —
// полосовой на частоте и Q полосы, у low shelf — ФНЧ, у high shelf — ФВЧ, Q 0,7071 в dB) → пик (STEREO — max |L|, |R|;
// MID — |L+R|/2; SIDE — |L−R|/2) с атакой 5 мс и спадом 100 мс → сдвиг усиления RANGE · min(1, (уровень − THRESHOLD) /
// 12 dB) на каждый отсчёт. Фильтр детектора сглаживается, как полосы EqUnit. На цепочку и полосу (c·bands + k) — свой.
#pragma once
#include <cstdint>
#include <vector>
#include "EqBiquads.h"
#include "EqModel.h"
#include "util/AtomicFloat.h"

namespace jambo {

class EqUnit;

class EqDetectors {
public:
    static constexpr int n = 2 * EqSpec::bands;
    static constexpr double attackMs = 5;
    static constexpr double releaseMs = 100;
    static constexpr double spanDb = 12;
    static constexpr double shelfQ = 0.7071067811865476;
    static constexpr int bandpass = 6;

    explicit EqDetectors(double sampleRate);

    double kAtt, kRel;
    std::vector<int> src, ms;
    std::vector<double> thr, range;
    std::vector<int> kind;
    std::vector<double> tf, tq, sf, sq;
    std::vector<int> el;
    std::vector<uint8_t> fresh, changed, moving;
    std::vector<double> co, coR, st, env;
    std::vector<float> off, last;
    std::vector<uint8_t> live, dirty;
    double ramp[10] = {};
    std::vector<float> bufL, bufR;

    static int detKind(int band);
    void reset(int i, const double* p, int band, double sr);
    void retarget(int i, const double* p, int band, double decay);
    void setLevels(int i, const double* p) {
        if (src[size_t(i)] < 0) return;
        thr[size_t(i)] = p[7];
        range[size_t(i)] = p[8];
    }
    /// Блок n отсчётов детектора i: источник (nullptr — тишина) → сдвиг усиления off[i].
    void run(int i, const float* l, const float* r, int n, double sr, double decay);
    static bool still(const double* z);

private:
    void filter(int i, float* l, float* r, int n, double sr, double decay);
    void coeffsAt(int i, double e, double sr, double decay, double* out);
};

/// Сдвиг усиления динамических полос для экрана: канал·bands + полоса, dB; пишет колбэк, экран читает по таймеру.
class EqDynLevels {
public:
    EqDynLevels() : v(size_t(EqSpec::channels * EqSpec::bands)) {}
    void publish(int ch, const EqUnit& u);
    float offset(int ch, int k) const {
        const int i = ch * EqSpec::bands + k;
        return i >= 0 && i < int(v.size()) ? v[size_t(i)].load() : 0;
    }

private:
    std::vector<AtomicFloat> v;
};

}  // namespace jambo
