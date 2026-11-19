// Delay: ленточный стерео-дилей, шина отдаёт только эхо (wet). Порт 1:1 Core/FX/Delay.swift (ветка swift JAMbo);
// поведение — docs/ai/fx-eq.md JAMbo. Совпадает с эталонами CoreTests/golden/fx_dly_*.i16 по отсчётам.
// Параметры — k-rate (на блок): time (с), feedback (0…1), filter (−1…1), spread (0…1).
#pragma once
#include <cstdint>
#include "FxRandom.h"

namespace jambo {

class FxDelay {
public:
    /// maxTime — наибольшее время, с (1 такт при 40 BPM — 6 с); seed — генератор модуляции.
    FxDelay(double sampleRate, double maxTime, uint32_t seed);
    ~FxDelay();
    FxDelay(const FxDelay&) = delete;
    FxDelay& operator=(const FxDelay&) = delete;

    /// Блок n отсчётов: вход (nullptr — тишина) → эхо в outL/outR (перезаписывает).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                 float time, float feedback, float filter, float spread);
    void reset();
    /// Покой: фильтры с нуля, время и ручки при пробуждении — сразу целевые.
    void sleep();

private:
    void updateFilter(double f);

    double sr_, maxTime_;
    int len_, mask_;
    float* lineL_;
    float* lineR_;
    int w_ = 0;
    double dL_ = 0, dR_ = 0;
    bool started_ = false;
    double fb_ = 0.4, filter_ = 0, spread_ = 0;
    double kHp_[5] = {1, 0, 0, 0, 0};
    double zHp_[8] = {};             // [x1, x2, y1, y2] L, затем R
    double cLp_[3] = {}, cHp_[3] = {};
    double sLp_[4] = {}, sHp_[4] = {};  // [ic1, ic2] L, затем R
    double wLp_ = 0, wHp_ = 0;
    double fSig_;
    FxRandom rng_;
    double wowPh_[2], wowA_[2], wowB_[2];
    double flR_, flI_;
    double flS_[2], flC_[2];
};

}  // namespace jambo
