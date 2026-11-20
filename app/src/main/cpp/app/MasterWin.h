// Данные дисплеев окон модулей мастера вне колбэка (порт TeleHistory из MasterDisplay.swift и SatSpectrumState из
// SatWindow.swift, ветка swift JAMbo): история телеметрии DynTele за 4 с (каждый столбец — максимум своего отрезка,
// FREEZE — снимок кольца); спектры входа и выхода сатуратора (1/6 октавы, подъём сразу, спад 30 dB/s).
#pragma once
#include <optional>
#include <vector>
#include "eq/Spectrum.h"
#include "master/DynTele.h"
#include "master/MasterChain.h"

namespace jambo {

class TeleHistory {
public:
    static constexpr int frames = 750;
    static constexpr double liveTime = 0.3;
    bool frozen = false;
    TeleHistory();
    void setFrozen(bool on, const DynTele& t);
    bool isLive(double now) const { return now - lastAt < liveTime; }
    struct Latest {
        float inPeak, det, gr;
    };
    std::optional<Latest> latest(const DynTele& t, double now) const;
    struct Columns {
        std::vector<float> inDb, outDb, gr;
    };
    Columns columns(const DynTele& t, int cols, double now);

private:
    std::vector<float> sIn, sGr, sOut;
    long sWritten = 0, lastWritten = -1;
    double lastAt = -1e9;
};

class SatSpectrumState {
public:
    static constexpr float bottom = -96;
    bool frozen = false;
    std::vector<float> dIn, dOut;
    /// Кадр: n столбцов (частоты — EqView::freq по доле ширины).
    void update(const MasterChain& m, int n, double now);

private:
    SpectrumAnalyzer pre, post;
    std::vector<double> cum = std::vector<double>(size_t(SpectrumAnalyzer::bins + 1), 0.0);
    double last = 0;
    std::vector<float> bands(const SpectrumAnalyzer& an, int n);
};

}  // namespace jambo
