// Лимитер мастера (порт Core/Master/Limiter.swift, ветка swift JAMbo): стерео, один gain computer по максимуму L и R.
// Вход · GAIN → true peak (4× FIR) → цель g = min(1, Ceiling/Peak) → скользящий минимум за окно lookahead + HOLD → релиз
// (RELEASE или AUTO 30/400 мс) → скользящее среднее окна → задержанный сигнал · g → страховочный клип на CEILING.
// BYPASS — 10 мс; смена LOOKAHEAD — кроссфейд задержек 20 мс; GUARD — средний GR выше предела убавляет вход.
#pragma once
#include <vector>
#include "MasterTables.h"

namespace jambo {

/// Gain computer: история цели и рампы, монотонная очередь скользящего минимума.
class LimiterGain {
public:
    static constexpr int hold = 8;
    LimiterGain(int maxL, int L);
    void setWindow(int L);
    double pushMin(int idx, double v);
    double step(double pk, double ceil, bool autoRel, double kRel, double kFast, double kSlow);

    const int maxL;
    std::vector<double> tHist, eHist;
    std::vector<int> qIdx;
    std::vector<double> qVal;
    int hpos = 0, qHead = 0, qLen = 0, n = 0, sumAge = 0;
    double envF = 1, envS = 1;
    int L;
    double sum;
};

class MasterLimiter {
public:
    static constexpr int taps = MasterTables::limTaps;
    static constexpr double maxLookaheadMs = 6, autoFastMs = 30, autoSlowMs = 400, smoothMs = 20, bypassMs = 10,
                            xfadeMs = 20;
    static constexpr int meterFrames = 256;
    static constexpr double guardAvgS = 1, guardDown = 1, guardUp = 0.3, guardOff = 3, guardHoldS = 2, guardMax = 12;

    explicit MasterLimiter(double sampleRate, const MasterTables& tables = MasterTables::shared());
    static int look(double ms, double sr, int maxL);
    int delayFor(int L) const { return L + MasterTables::limFirDelay + LimiterGain::hold / 2; }
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, double gain, double ceiling,
                 double lookahead, double rel, bool autoRel, double bypass, double guardDb = 0);

    double guardCut = 0;
    double frameIn = 0, frameGr = 0, frameOut = 0;
    long frames = 0;

private:
    double sr;
    const double* phases;
    double kSmooth, wetStep, xfStep, kFast, kSlow, kRel, relSig = 150;
    int maxL, size;
    std::vector<float> dl, dr, xl, xr;
    std::vector<double> hIn, hOut;
    int posIn = 0, posOut = 0, w = 0;
    double pkPrev = 0, gainLin = 1, ceil, wet = 1;
    double guardAvg = 0, guardHold = 0;
    int L;
    int oldDelay = -1;
    double xf = 1;
    LimiterGain gc;
    int meterN = 0;
    double mIn = 0, mGr = 1, mOut = 0;

    double truePeak(double* h, int& pos, double l, double r);
    void guardStep(double limit, double gr, double dt);
};

}  // namespace jambo
