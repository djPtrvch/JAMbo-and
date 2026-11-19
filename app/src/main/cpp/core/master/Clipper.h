// Клиппер мастера (порт Core/Master/Clipper.swift, ветка swift JAMbo): стерео, каналы независимо. Вход · DRIVE →
// кроссовер LR4 на BASS (0 — OFF) → ВЧ: 4× оверсэмплинг → мягкое колено (SOFTNESS) по CEILING с антипроизводной и
// коррекцией [−1/8, 5/4, −1/8] → децимация; НЧ — без обработки, с задержкой 32 → сумма. BYPASS — кроссфейд 10 мс.
// Индикация — кадр 256 отсчётов: пик после DRIVE, срез (окна 10 мс), пик выхода.
#pragma once
#include <vector>
#include "fx/DriveTables.h"

namespace jambo {

class MasterClipper {
public:
    static constexpr double smoothMs = 20, bypassMs = 10, grWindowMs = 10, tiny = 1e-25;
    static constexpr int meterFrames = 256;
    static constexpr int lfDelay = 32, ring = 33;
    static constexpr int oLp = 0, oHp = 4, oHist = 8, oOs = 74, oLf = 332, oDry = 365, oHf = 398;
    static constexpr int oPrevU = 431, oPrevF = 432, oC0 = 433, oC1 = 434;
    static constexpr int chSize = 435, iSize = 3;

    explicit MasterClipper(double sampleRate, const DriveTables& tables = DriveTables::shared());
    static void butter(double* o, double f, double sr, bool high);
    static double staticOut(double x, double input, double ceiling, double softness);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, double drive, double ceiling,
                 double softness, double bass, double bypass);

    double frameIn = 0, frameGr = 0, frameOut = 0;
    long frames = 0;

private:
    double sr;
    const DriveTables& d;
    std::vector<double> st;
    std::vector<int> ist;
    double cf[30] = {};
    double kSmooth, kBlock, wetStep, splitStep;
    int grWin;
    double driveLin = 1, ceil, knee, freq = 100, split = 1, wet = 1;
    bool engaged = false;
    int grN = 0;
    double winIn = 0, winOut = 0, lastGr = 0, frameGrAcc = -1;
    int meterN = 0;
    double mIn = 0, mOut = 0;

    static double shape(double u, double k);
    static double antideriv(double u, double k);
    double hf(double* c, int* ic, double x, double ceil, double k);
    static double biquad(double* s, const double* c, double x) {
        const double y = c[0] * x + s[0];
        s[0] = c[1] * x - c[3] * y + s[1];
        s[1] = c[2] * x - c[4] * y;
        return y;
    }
    void closeWindow();
};

}  // namespace jambo
