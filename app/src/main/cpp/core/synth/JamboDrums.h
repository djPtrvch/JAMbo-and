// Ударные JAMbo: SNARE, HAT, CLAP, PERC (20–23). Порт 1:1 Core/Jambo/JamboDrums.swift (ветка swift JAMbo);
// поведение — docs/ai/synth-engines.md JAMbo. Затухают сами, монофоничны (слот 0): новый удар обрывает прошлый —
// хвост гаснет за ~2 мс. Выход — через tanh. Эталонов нет — проверка тестами свойств (tests/drums_test.cpp).
#pragma once
#include <cstdint>
#include "Jambo.h"
#include "JamboTables.h"

namespace jambo {

class JamboDrumVoice {
public:
    JamboDrumVoice(const JamboTables& tables, double sampleRate);

    void set(double q1, double q2, double q3, double q4, double decay) { t1_ = q1; t2_ = q2; t3_ = q3; t4_ = q4; td_ = decay; }
    /// Удар движка e; прошлый удар того же голоса гаснет за ~2 мс.
    void trigger(int engine, double q1, double q2, double q3, double q4, double decay);
    /// Кусок n кадров (n ≤ 24).
    void render(float* out, int n);

    int engine() const { return engine_; }
    double last = 0;

private:
    void planBursts();
    void snare(float* out, int n);
    void hat(float* out, int n);
    void clap(float* out, int n);
    void perc(float* out, int n);

    const JamboTables& tables_;
    double sr_;
    int engine_ = J::snare;
    double t1_ = 0.5, t2_ = 0.5, t3_ = 0.5, t4_ = 0.5, td_ = 300;
    double s1_ = 0.5, s2_ = 0.5, s3_ = 0.5, s4_ = 0.5, sd_ = 300;
    double t_ = 0;
    int64_t rnd_ = 12345;
    double dc_ = 0;
    double ph_[6] = {};
    double ic_[6] = {};
    double hb_[J::hbLen * 2] = {};
    int hbPos_ = 0;
    double burstAt_[6] = {};
    int bursts_ = 4;
    double envC_ = 0, envT_ = 0;
    double lp_ = 0;
};

}  // namespace jambo
