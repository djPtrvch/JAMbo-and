// Сатуратор мастера (порт Core/Master/Saturator.swift, ветка swift JAMbo): стерео, каналы независимо. Вход · DRIVE → 4×
// оверсэмплинг → TAPE / TUBE / EDGE с антипроизводной ÷ компенсация громкости → коррекция среднего → децимация → фильтр
// DC (TUBE) → TONE (shelf 1 кГц) → смесь с сухим (MIX · smoothstep(DRIVE, 0…1 dB)). FOCUS: x = LP + HP (20 Гц — OFF),
// кривая — на HP. DRIVE 0 или MIX 0 — выход = сухой с задержкой 33. Расчёт — Double.
#pragma once
#include <vector>
#include "MasterTables.h"

namespace jambo {

class MasterSaturator {
public:
    static constexpr double fadeInDb = 1, toneHz = 1000, toneQ = 0.5, toneMaxDb = 6, dcHz = 5, smoothMs = 20;
    static constexpr double typeFadeMs = 20, tiny = 1e-25, adaaEps = 1e-6, warmRampMs = 5, focusOff = 20;
    static constexpr int warmSamples = 40;
    static constexpr int dryDelay = 33;
    static constexpr int corrLen = 8;
    static constexpr int oHist = 0, oU = 66, oY = 70, oCorr = 74, oOs = 90, oPrevF = 348, oDry = 351, oLpD = 384;
    static constexpr int oPrevU = 417, oDcX = 418, oDcY = 419, oLo = 420, oHi = 422, oLp = 424;
    static constexpr int chSize = 425;
    static constexpr int iSize = 4;

    explicit MasterSaturator(double sampleRate, const MasterTables& tables = MasterTables::shared());
    /// Блок n отсчётов (вход и выход могут совпадать). drive 0…24 dB, type 0–2, tone −100…100, mix 0…100, focus 20…2000.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, double drive, double type,
                 double tone, double mix, double focus);
    static void shelf(double* o, double gDb, double sr, bool high);

private:
    double sr;
    const MasterTables& t;
    std::vector<double> st;
    std::vector<int> ist;
    double cLo[5], cHi[5];
    double kSmooth, kBlock, fadeStep, splitStep, rampStep, kDc;
    double driveDb = 0, mix = 1, toneDb = 0, focus = 20, split = 0, kLp = 0;
    int cur = 0, prev = 0;
    double fade = 1;
    bool idle = true;
    int warmN = 0;
    double ramp = 1;

    double anti(int type, double u) const;
    void shape(double* c, int type, double k, bool add);
    double wet(double* c, int* ic, double x, int ta, int tb, double wa, double wb, double ca, double cb);
    void processIdle(const float* inL, const float* inR, float* outL, float* outR, int n, double driveT, double mixT,
                     double splitT, double kLp0, double kLpStep);
};

}  // namespace jambo
