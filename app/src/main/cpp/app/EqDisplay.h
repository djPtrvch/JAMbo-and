// Спектры и кривые на поле EQ (порт SpectrumDisplay из UI/EqSpectrum.swift и EqPlot.curves, ветка swift JAMbo):
// среднее по 1/6 октавы, подъём сразу, спад 30 dB/s; после EQ = до EQ + действие EQ (разница, сглаженная за ~50 мс).
// Координаты — доли поля: x — частота 10 Гц…20 кГц логарифмически, y — 0 сверху.
#pragma once
#include "eq/EqModel.h"
#include "eq/Spectrum.h"
#include <vector>

namespace jambo {

struct EqView {
    static constexpr double dbRange = 21, low = 10;
    static double octaves() { return std::log2(EqSpec::freqMax / low); }
    /// Частота по доле ширины u.
    static double freq(double u) { return low * std::pow(2.0, u * octaves()); }
    static double u(double f) { return std::log2(f / low) / octaves(); }
};

class SpectrumDisplay {
public:
    static constexpr float top = 6, bottom = -90, fall = 30;
    static constexpr double diffTau = 0.05;
    /// Кадр: n столбцов, dt — с последнего кадра; pre и post — y (доли высоты) по столбцам.
    void frame(const Spectra& s, int n, double dt, std::vector<float>& pre, std::vector<float>& post);

private:
    SpectrumAnalyzer a_, b_;
    std::vector<double> cum_ = std::vector<double>(size_t(SpectrumAnalyzer::bins + 1));
    std::vector<float> dPre_, dPost_, dDiff_;
    std::vector<float> bands(const SpectrumAnalyzer& an, int n);
};

/// Суммарные кривые включённых полос в dB по n точкам: стерео, середина (стерео + MID), бока (стерео + SIDE).
struct EqCurves {
    std::vector<double> stereo, mid, side;
    bool hasStereo = false, hasMid = false, hasSide = false;
};
EqCurves eqCurves(const Eq& eq, int n, const std::vector<double>& offs);

}  // namespace jambo
