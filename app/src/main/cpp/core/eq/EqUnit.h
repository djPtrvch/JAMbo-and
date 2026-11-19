// EQ одного канала в колбэке (порт Core/EQ/EqUnit.swift, EqUnitDyn.swift, EqUnitMs.swift; ветка swift JAMbo): полосы —
// биквады (EqCoeffs; коэффициенты и состояние в Double, между ступенями — Float); low/high cut 24 и 48 dB/окт — каскад
// из 2 и 4 биквадов. Частота, усиление и Q сглаживаются (экспонента, τ 20 мс) квантами по 128 отсчётов: L и R — свои
// отрезки траектории (как у эталонов). Смена набора полос — новая цепочка и кроссфейд 40 мс; хвост прежней звучит до
// затухания. Линии — стерео-сигналы с одним EQ (дорожка: сухой сигнал и посылы). Полосы MID/SIDE — между L/R → M/S и
// обратно; динамические полосы — детекторы EqDetectors и коэффициенты на каждый отсчёт. Память — при создании.
#pragma once
#include <cstdint>
#include <vector>
#include "EqBiquads.h"
#include "EqDyn.h"
#include "EqModel.h"

namespace jambo {

class EqUnit {
public:
    static constexpr int bands = EqSpec::bands;
    static constexpr int maxStages = 4;
    static constexpr int fadeFrames = 1920;
    static constexpr double tau = 0.02;
    static constexpr int sub = EqDsp::sub;
    static constexpr int quantum = EqDsp::quantum;
    static constexpr int maxFrames = EqDsp::maxFrames;
    static constexpr int bandState = maxStages * 8;
    static constexpr double quiet = EqDsp::quiet;

    explicit EqUnit(double sampleRate, int lanes = 1);
    EqUnit(const EqUnit&) = delete;
    EqUnit& operator=(const EqUnit&) = delete;

    const int lanes;
    /// Выходы дорожек — источники T1…T16 (дорожка t — trackL + t·trackStride; nullptr — тишина).
    const float* trackL = nullptr;
    const float* trackR = nullptr;
    int trackStride = 0;
    /// Сигналы линий на этот блок (ставит вызывающий; nullptr — линии нет).
    std::vector<float*> laneL, laneR;

    bool ringing(int lane) const { return ring[size_t(lane)] != 0; }
    /// Один стерео-сигнал (линия 0) на месте.
    void process(float* left, float* right, int frames, const double* p);
    /// Блок на месте во всех линиях; p — параметры канала (EqSpec::stride значений).
    void process(int frames, const double* p);
    /// Канал молчит: набор полос и значения — сразу; детекторы слушают свои источники frames отсчётов.
    void idle(const double* p, int frames = 0);
    /// Сдвиг усиления полосы k текущей цепочки в конце блока, dB.
    float dynOffset(int k) const { return det.last[size_t(cur * bands + k)]; }

private:
    double sr;
    double decay;
    std::vector<int> kind, nst, msm;
    std::vector<double> tf, tg, tq, sf, sg, sq;
    std::vector<int> el;
    std::vector<uint8_t> fresh, changed, moving;
    std::vector<double> co, coR;
    double ramp[10] = {};
    std::vector<double> st;
    std::vector<float> oldL, oldR, midBuf, sideBuf;
    EqDetectors det;
    std::vector<float> selfL, selfR;
    std::vector<double> dynL, dynR;
    std::vector<uint8_t> live, ring;
    int cur = 0;
    int fadePos = -1;

    bool same(const double* p, int c) const;
    void startFade(const double* p);
    void setTargets(const double* p, int c);
    void snap(int c);
    static bool silent(const float* l, const float* r, int n);
    bool settle(int lane);
    void fadeIn(int n);
    void mixFade(int n);
    void endFade(int n);
    void run(int c, int n, bool old);
    bool signal(int lane, bool old, float*& l, float*& r);
    void runBand(int c, int i, int n, bool old);
    void runMsBand(int c, int i, int n, bool old);
    void coeffsAt(int i, double e, double* out);
    // Динамика (EqUnitDyn.cpp).
    void runDetectors(int c, int n);
    static bool nonZero(const float* x, int n);
    void coeffsAtDyn(int i, bool mov, double e, double off, double* out);
    void fillCoeffs(int i, bool mov, int e0, const float* off, int m, double* out);
};

}  // namespace jambo
