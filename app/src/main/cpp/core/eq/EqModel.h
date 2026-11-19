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
/// Типов полос, крутизн среза, режимов M/S (EqSpec.types, slopes, msModes).
constexpr int typeCount = 6, slopeCount = 3, msCount = 3;
constexpr double freqMin = 20, freqMax = 20000, gainMax = 18, qMin = 0.1, qMax = 18, qDef = 1;
inline bool isCut(int type) { return type == lowCut || type == highCut; }
/// Усиление не применяется к cut и notch.
inline bool hasGain(int type) { return type <= highShelf; }
inline int slopeDb(int k) { static const int s[slopeCount] = {12, 24, 48}; return s[k]; }
/// Каналы снимка: дорожки 0…maxTracks−1 (16), мастер — 16.
constexpr int channels = 16 + 1;
constexpr int masterChannel = 16;
/// Окно коррекции наушников (спектры до и после монитор-цепочки; PhonesEq.swift).
constexpr int phonesChannel = -2;
/// Параметры EQ для колбэка: на канал stride значений — [0] включён ли EQ, затем на полосу [тип (−1 — нет), каскад,
/// частота, усиление, Q фильтра, режим M/S, источник Dynamic (−1 — нет), порог, сдвиг].
constexpr int bandStride = 9;
constexpr int stride = 1 + bands * bandStride;
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
    /// Места под полосы; номера в пределах таблиц; источник Dynamic — существующая дорожка (иначе OFF).
    void repair(int n) {
        bands.resize(size_t(EqSpec::bands));
        for (auto& ob : bands) {
            if (!ob) continue;
            EqBand& b = *ob;
            b.type = std::min(EqSpec::typeCount - 1, std::max(0, b.type));
            b.slope = std::min(EqSpec::slopeCount - 1, std::max(0, b.slope));
            b.ms = std::min(EqSpec::msCount - 1, std::max(0, b.ms));
            if (b.dyn > n || b.dyn < -1) b.dyn = -1;
        }
    }
    // Правки (EqModel.swift: extension Eq).
    /// Новая bell-полоса в первом свободном месте; −1, если мест нет.
    int addBand(double freq, double gain) {
        for (int k = 0; k < int(bands.size()); ++k) {
            if (bands[size_t(k)]) continue;
            bands[size_t(k)] = EqBand(true, EqSpec::bell, clamp(freq, EqSpec::freqMin, EqSpec::freqMax),
                                      clamp(gain, -EqSpec::gainMax, EqSpec::gainMax), EqSpec::qDef);
            return k;
        }
        return -1;
    }
    /// Значения полосы: частота, усиление (округление до 0,1 dB), Q.
    void setBand(int k, std::optional<double> freq, std::optional<double> gain = std::nullopt,
                 std::optional<double> q = std::nullopt) {
        change(k, [&](EqBand& b) {
            if (freq) b.freq = clamp(*freq, EqSpec::freqMin, EqSpec::freqMax);
            if (gain) b.gain = std::round(clamp(*gain, -EqSpec::gainMax, EqSpec::gainMax) * 10) / 10;
            if (q) b.q = clamp(*q, EqSpec::qMin, EqSpec::qMax);
        });
    }
    void setType(int k, int type) {
        const int n = EqSpec::typeCount;
        change(k, [&](EqBand& b) { b.type = ((type % n) + n) % n; });
    }
    void setSlope(int k, int slope) {
        change(k, [&](EqBand& b) { b.slope = std::max(0, std::min(slope, EqSpec::slopeCount - 1)); });
    }
    /// Режим M/S полосы: 0 STEREO, 1 MID, 2 SIDE.
    void setMs(int k, int ms) {
        change(k, [&](EqBand& b) { b.ms = std::max(0, std::min(ms, EqSpec::msCount - 1)); });
    }
    /// Источник динамической полосы: −1 OFF, 0 SELF, n — дорожка n.
    void setDyn(int k, int src) {
        change(k, [&](EqBand& b) { b.dyn = std::max(-1, std::min(src, EqSpec::channels - 1)); });
    }
    /// Порог (dBFS) и сдвиг (dB) динамической полосы, с округлением до 0,1.
    void setDynParams(int k, std::optional<double> thr, std::optional<double> range = std::nullopt) {
        change(k, [&](EqBand& b) {
            if (thr) b.thr = std::round(clamp(*thr, EqSpec::dynThrMin, 0) * 10) / 10;
            if (range) b.range = std::round(clamp(*range, -EqSpec::dynRangeMax, EqSpec::dynRangeMax) * 10) / 10;
        });
    }
    void toggleBand(int k) { change(k, [](EqBand& b) { b.on = !b.on; }); }
    void deleteBand(int k) {
        if (k >= 0 && k < int(bands.size())) bands[size_t(k)].reset();
    }

    auto tie() const { return std::tie(on, bands); }
    JAMBO_EQ(Eq)

private:
    static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
    template <typename F>
    void change(int k, F&& f) {
        if (k < 0 || k >= int(bands.size()) || !bands[size_t(k)]) return;
        f(*bands[size_t(k)]);
    }
};

}  // namespace jambo
