// Drive: искажение на шине посыла (только обработанный сигнал), стерео, каналы независимо.
// Порт 1:1 Core/FX/Drive.swift (ветка swift JAMbo); поведение — docs/ai/fx-eq.md JAMbo.
// Совпадает с эталонами CoreTests/golden/fx_drv_*.i16 по отсчётам.
// Параметры — k-rate: drive (0–100), type (0–5: SOFT, HARD, TUBE, FUZZ, FOLD, CRUSH), tone (0–100), lowcut (Гц).
#pragma once
#include "DriveTables.h"

namespace jambo {

class FxDrive {
public:
    explicit FxDrive(double sampleRate, const DriveTables& tables = DriveTables::shared());

    /// Блок n отсчётов: вход (nullptr — тишина) → outL/outR (перезаписывает).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                 float drive, float type, float tone, float lowcut);
    /// Состояния каналов — с нуля.
    void reset();

    static constexpr int soft = 0, hard = 1, tube = 2, fuzz = 3, fold = 4, crush = 5;

private:
    double anti(int type, double u) const;
    void shape(double* c, int type, double k) const;
    double wet(double* c, int* ic, double x, int ta, int tb, double wa, double wb, double ka, double kb) const;

    // Раскладка состояния канала (Double): история 66, u 4, y 4, коррекция 16, децимация 258, prevF 4,
    // задержка CRUSH 33, затем prevU, prevS, hold, dcX, dcY, LOW CUT [s1, s2], TONE [s1, s2].
    static constexpr int oHist = 0, oU = 66, oY = 70, oCorr = 74, oOs = 90, oPrevF = 348, oCd = 352;
    static constexpr int oPrevU = 385, oPrevS = 386, oHold = 387, oDcX = 388, oDcY = 389, oLc = 390, oTone = 392;
    static constexpr int chSize = 394;
    // Целые состояния канала: hpos, cpos, opos, cdp, holdN.
    static constexpr int iSize = 5;

    double sr_;
    const DriveTables& t_;
    bool started_ = false;
    double drive_ = 26, tone_ = 50, lowcut_ = 20;
    double kSmooth_;
    int cur_ = 0, prev_ = 0;
    double fade_ = 1;
    double fadeStep_;
    double kDc_;
    double st_[2 * chSize] = {};
    int ist_[2 * iSize] = {};
    double cLc_[5] = {}, cTone_[5] = {};
    double lcSig_, toneSig_;
    double lcW_ = 0;
    double silent_ = 0;
};

}  // namespace jambo
