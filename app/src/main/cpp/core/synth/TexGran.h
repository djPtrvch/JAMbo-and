// Формулы GRANULAR (порт TexGran из Core/Model/TexGran.swift, ветка swift JAMbo; без подписей ручек — они в UI).
#pragma once
#include <algorithm>
#include <cmath>

namespace jambo {
namespace TexGran {

/// Длина зерна, с: 0,01 · 80^v (10…800 мс).
inline double grainSize(double v) { return 0.01 * std::pow(80.0, v); }
/// Зёрен звучит одновременно: 0,5 · 48^v (0,5…24).
inline double overlap(double v) { return 0.5 * std::pow(48.0, v); }
/// Зёрен в секунду на ноту — не больше.
constexpr double maxRate = 100.0;

inline double smooth(double a, double b, double x) {
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}
/// Разброс высоты зерна, центы (×rnd).
inline double centsSpread(double sp) { return 25 * sp * sp + 120 * smooth(0.75, 1, sp); }
/// Разброс панорамы (×rnd, затем clamp −1…1).
inline double panSpread(double sp) { return 0.1 + 0.6 * sp * sp; }
/// Разброс позиции, доля сэмпла (×rnd).
inline double posSpread(double sp) { return 0.2 * sp * sp; }
/// Дрожание интервала (×rnd).
inline double jitter(double sp) {
    const double s = smooth(0.4, 1, sp);
    return 0.4 * s * s;
}

struct State { double dur, eff, rate; };
/// Длина зерна (с), зёрен одновременно и частота запуска на ноту; sc — min(1; 4 / N).
inline State state(double v, double d, double sc, double len) {
    const double dur = std::min(grainSize(v), len * 0.9);
    const double eff = std::min(overlap(d) * sc, maxRate * dur);
    return {dur, eff, dur > 0 ? eff / dur : 0};
}

constexpr double gainK = 1.3;
constexpr double out = 0.63 * 2.8;

struct Plan { double iv, gain; };
/// Интервал запуска (с) и усиление шины ноты. ts — период источника, с (0 — неизвестен), semi — полутоны.
inline Plan plan(double dur, double eff, double ts, double semi, double sp) {
    double iv = eff > 0 ? dur / eff : dur;
    if (ts > 0) {
        const double tout = ts / std::pow(2.0, semi / 12);
        iv = std::max(1.0, std::round(iv / tout)) * tout;
    }
    const double effA = dur / iv;
    const double inc = gainK / std::sqrt(std::max(1.0, effA));
    if (ts <= 0) return {iv, inc};
    const double coh = 1.5 * gainK / std::max(1.0, effA);
    return {iv, coh + (inc - coh) * smooth(0.3, 0.7, sp)};
}

}  // namespace TexGran
}  // namespace jambo
