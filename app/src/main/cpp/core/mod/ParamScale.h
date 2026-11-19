// Шкала ручки без строк и функций — для колбэка; цели модуляции дорожки (порт ParamScale и ModTarget,
// Core/Mod/ModEngine.swift, ветка swift JAMbo).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "model/Params.h"
#include "sampler/VoiceParam.h"

namespace jambo {

struct ParamScale {
    double min = 0, max = 1;
    /// Степень (0 — нет).
    double curve = 0;
    bool exp = false;
    bool stepped = false;
    /// Пунктов у ступенчатой с непрерывным значением (0 — нет).
    int steps = 0;

    ParamScale() = default;
    explicit ParamScale(const ParamDef& p)
        : min(p.min), max(p.max), curve(p.curve.value_or(0)), exp(p.exp), stepped(p.kind == KnobKind::stepped),
          steps(p.steps.value_or(0)) {}

    double toNorm(double v) const {
        if (exp) return std::log(v / min) / std::log(max / min);
        const double lin = std::min(1.0, std::max(0.0, (v - min) / (max - min)));
        if (curve > 0) return std::pow(lin, 1 / curve);
        return lin;
    }

    double fromNorm(double n0) const {
        const double n = std::min(1.0, std::max(0.0, n0));
        if (steps > 0) {
            const int i = std::min(steps - 1, std::max(0, int(std::floor(n * double(steps)))));
            return min + (double(i) + 0.5) / double(steps) * (max - min);
        }
        if (stepped) return std::round(n * (max - min)) + min;
        if (exp) return min * std::pow(max / min, n);
        if (curve > 0) return min + std::pow(n, curve) * (max - min);
        return min + n * (max - min);
    }
};

/// Цели модуляции дорожки: параметры голоса, затем Chance, Ratchet.
namespace ModTarget {
constexpr int prob = VoiceParam::count;
constexpr int ratchet = VoiceParam::count + 1;
constexpr int count = VoiceParam::count + 2;
inline const std::vector<std::string>& ids() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> a = VoiceParam::ids();
        a.push_back("prob");
        a.push_back("ratchet");
        return a;
    }();
    return v;
}
}  // namespace ModTarget

}  // namespace jambo
