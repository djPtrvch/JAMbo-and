// EQ канала: 10 мест под полосы (порт EqBand, Eq и констант EqSpec, Core/EQ/EqModel.swift, ветка swift JAMbo;
// расчёт для колбэка — с EQ).
#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include "../model/Eqs.h"

namespace jambo {

namespace EqSpec {
constexpr int bell = 0, lowShelf = 1, highShelf = 2, lowCut = 3, highCut = 4, notch = 5;
constexpr double dynThrDef = -24, dynThrMin = -60, dynRangeDef = -6, dynRangeMax = 18;
constexpr int bands = 10;
constexpr double freqMin = 20, freqMax = 20000, gainMax = 18, qMin = 0.1, qMax = 18, qDef = 1;
inline bool isCut(int type) { return type == lowCut || type == highCut; }
/// Усиление не применяется к cut и notch.
inline bool hasGain(int type) { return type <= highShelf; }
}  // namespace EqSpec

struct EqBand {
    bool on = true;
    int type = 0;
    double freq = 1000, gain = 0, q = 1;
    int slope = 0;
    int ms = 0;
    /// Динамика: источник −1 OFF, 0 SELF, n — дорожка n; порог, dBFS; сдвиг, dB.
    int dyn = -1;
    double thr = EqSpec::dynThrDef;
    double range = EqSpec::dynRangeDef;

    EqBand() = default;
    EqBand(bool on_, int type_, double f, double g, double q_, int slope_ = 0, int ms_ = 0)
        : on(on_), type(type_), freq(f), gain(g), q(q_), slope(slope_), ms(ms_) {}
    auto tie() const { return std::tie(on, type, freq, gain, q, slope, ms, dyn, thr, range); }
    JAMBO_EQ(EqBand)
};

struct Eq {
    bool on = true;
    std::vector<std::optional<EqBand>> bands = std::vector<std::optional<EqBand>>(EqSpec::bands);

    /// Полоса k (nullopt — нет места или полосы).
    std::optional<EqBand> band(int k) const {
        return k >= 0 && k < int(bands.size()) ? bands[size_t(k)] : std::nullopt;
    }
    /// Полосы пресета (Eq.apply(EqPreset) в Swift; BYPASS не меняется); источник DYN больше tracks — OFF.
    void apply(const std::vector<std::optional<EqBand>>& p, int tracks) {
        for (int k = 0; k < EqSpec::bands; ++k) {
            std::optional<EqBand> b = k < int(p.size()) ? p[size_t(k)] : std::nullopt;
            if (b && b->dyn > tracks) b->dyn = -1;
            if (k < int(bands.size())) bands[size_t(k)] = b;
        }
    }
    auto tie() const { return std::tie(on, bands); }
    JAMBO_EQ(Eq)
};

}  // namespace jambo
