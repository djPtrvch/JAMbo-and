// Pitch по нотам у ударных (порт Core/Model/PitchKey.swift без drumDefs — подписей ручки, ветка swift JAMbo):
// включено — C3 звучит на высоте ручки, другие ноты — на столько же полутонов выше или ниже.
#pragma once
#include <algorithm>
#include <cmath>
#include "Params.h"
#include "../synth/Jambo.h"

namespace jambo {
namespace PitchKey {

/// Нота, на которой звучит высота ручки (как C3 у сэмпла).
constexpr double root = double(Params::sampleRoot);

/// Движок со слежением за нотами (ручка 1.1).
inline bool drum(int e) { return e == J::drum || e == J::snare || e == J::hat || e == J::clap || e == J::perc; }

struct Range {
    double lo, hi;
};
/// Пределы частоты 1.1 (Гц, логарифмически по ходу); у HAT — 0 (TUNE ±1 октава).
inline Range range(int e) {
    if (e == J::drum) return {J::drumPitchMin, J::drumPitchMax};
    if (e == J::snare) return {120, 400};
    if (e == J::perc) return {60, 2000};
    if (e == J::clap) return {700, 3000};
    return {0, 0};
}

/// Колбэк: положение 1.1 (n — значение / 100) для ноты note.
inline double knob(int e, double n, double note) {
    const double k = note - root;
    if (k == 0) return n;
    const Range r = range(e);
    if (r.lo <= 0) return std::max(-1.0, std::min(2.0, n + k / 24));
    const double span = std::log(r.hi / r.lo);
    if (e == J::clap) {
        // TONE = 700 · (3000/700)^(n^1,69).
        const double x0 = std::pow(std::max(0.0, std::min(1.0, n)), 1.69);
        const double x = x0 + k * std::log(2.0) / 12 / span;
        return std::pow(std::max(0.0, std::min(1.0, x)), 1 / 1.69);
    }
    const double v = n + k * std::log(2.0) / 12 / span;
    return std::max(-1.0, std::min(2.0, v));
}

}  // namespace PitchKey
}  // namespace jambo
