// Компрессор мастера «glue» (порт Core/Master/Glue.swift, ветка swift JAMbo): стерео, нулевая задержка. Детектор: 60 %
// пика + 40 % RMS (10 мс) после SC HPF; gain computer в dB с коленом KNEE и наклоном 1 − 1/RATIO; GR сглаживается
// (ATTACK, RELEASE или AUTO — 60 мс и 1,2 с). Выход = вход·(1 − MIX) + вход·GR·Makeup·MIX; BYPASS — 10 мс. Индикация —
// кадр 256 отсчётов; AUTO MAKEUP — суммы квадратов после фильтра K.
#pragma once
#include <cmath>

namespace jambo {

class MasterGlue {
public:
    static constexpr double peakWeight = 0.6, rmsWeight = 0.4, rmsMs = 10, rmsCal = 1.4142135623730951;
    static constexpr double peakReleaseMs = 10, autoFastMs = 60, autoSlowMs = 1200, smoothMs = 20, bypassMs = 10;
    static constexpr double scHpfOff = 20, tiny = 1e-25;
    static constexpr int meterFrames = 256;

    explicit MasterGlue(double sampleRate);
    void resetMeasure();
    static double coef(double ms, double sr) { return ms <= 0 ? 0 : std::exp(-1 / ((ms / 1000) * sr)); }
    static double staticGr(double level, double thr, double ratio, double knee);
    static double nextRatio(double r);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, double thr, double att,
                 double rel, bool autoRel, double makeup, double ratio, double knee, double mix, double schpf,
                 double bypass);

    double frameIn = 0, frameDet = -240, frameGr = 0, frameOut = 0;
    long frames = 0;
    bool measuring = false;
    double kSumIn = 0, kSumOut = 0;
    long kCount = 0;

private:
    double sr, kSmooth, wetStep, kPeak, kRms, kAutoFast, kAutoSlow;
    double kAtt = 0, kRel = 0, attSig = NAN, relSig = NAN;
    double thr = -10, makeupDb = 0, slope = 0.5, knee = 6, mix = 1, schpf = 90, wet = 1, hpfSig = NAN;
    double hb0 = 0, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0;
    double zl1 = 0, zl2 = 0, zl3 = 0, zl4 = 0, zr1 = 0, zr2 = 0, zr3 = 0, zr4 = 0;
    double pk = 0, ms = 0, env = 0, envSlow = 0, ldb = -240;
    int meterN = 0;
    double mIn = 0, mGr = 0, mOut = 0, mDet = -240;
    double kf[16] = {};

    void updateHpf(double f);
    double kWeigh(double x, int o);
};

}  // namespace jambo
