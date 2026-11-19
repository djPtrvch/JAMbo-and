// Прослушивание полосы EQ (порт Core/EQ/EqListen.swift, ветка swift JAMbo): пока держат LISTEN, канал EQ звучит только в
// области выбранной полосы — после своего EQ фильтр (bell и notch — полосовой, low cut/shelf — ФНЧ, high cut/shelf —
// ФВЧ, 12 dB/окт); MID — середина на оба канала, SIDE — бока (L = S, R = −S). Включение и выключение — 10 мс, частота —
// сглаживание по блокам (τ 20 мс). Цель ставит UI (атомарные значения), остальное — в колбэке.
#pragma once
#include <limits>
#include <vector>
#include "EqModel.h"
#include "util/AtomicFloat.h"

namespace jambo {

class EqUnit;

class EqListen {
public:
    static constexpr int bandpass = 0, lowpass = 1, highpass = 2;
    static constexpr float fadeFrames = 480;
    static constexpr double tau = 0.02;
    static constexpr double butterQ = -3.0102999566398116;
    static constexpr int maxLanes = 4;

    explicit EqListen(double sampleRate);

    AtomicCounter channel, kind, ms;
    AtomicFloat freq{1000}, q{1};
    /// Канал, который сейчас звучит через фильтр (−1 — нет).
    int active = -1;

    struct Filter {
        int kind;
        double q;
    };
    static Filter filter(const EqBand& b);
    static void coeffs(int kind, double hz, double q, double sr, double* out);
    struct Span {
        double lo, hi;
    };
    static Span span(const EqBand& b);
    /// Канал ch молчит в этом блоке: если он звучал через фильтр — отпустить сразу.
    void idle(int ch) {
        if (active == ch) {
            active = -1;
            mix = 0;
        }
    }
    /// Блок канала ch после его EQ, на месте во всех линиях u.
    void process(int ch, EqUnit& u, int frames);

private:
    double sr;
    float mix = 0;
    int curKind = -1;
    double curFreq = 1000, curQ = 1;
    int curMs = 0;
    double co[5] = {}, dc[5] = {}, next[5] = {};
    std::vector<double> st;
    std::vector<float> tmpL, tmpR;

    void prepare(int n);
    void hold();
    void run(int lane, float* l, float* r, int n, float m0, bool on);
};

}  // namespace jambo
