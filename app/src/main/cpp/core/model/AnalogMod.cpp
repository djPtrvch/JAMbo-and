// ANALOG: режимы модуляции и подписи ручек (порт Core/Model/AnalogMod.swift, ветка swift JAMbo).
#include "AnalogMod.h"
#include "../synth/Jambo.h"
#include <cstdio>

namespace jambo {
namespace AnalogMod {

const std::vector<std::string> names = {"Sync", "FM", "Ring", "AM"};
/// Порядок режимов по SHIFT + тап по 1.3.
static const int order[4] = {1, 4, 2, 3};

double toggled(double v) {
    const int m = mode(v);
    if (m > 0) return -double(m);
    const int last = int(-v + 0.5);
    return double(last >= 1 && last <= 4 ? last : 1);
}

double cycled(double v, bool back) {
    const int m = mode(v);
    if (m == 0) return v;
    int at = 0;
    for (int i = 0; i < 4; ++i)
        if (order[i] == m) at = i;
    const int next = back ? (at + 3) % 4 : (at + 1) % 4;
    return double(order[next]);
}

/// Ручка 1.3 при включённой модуляции: AMOUNT с подписью режима (SYNC — полутоны, FM и RING — проценты).
static const std::vector<ParamDef>& amount() {
    static const std::vector<ParamDef> v = [] {
        std::vector<ParamDef> out;
        for (int k = 0; k < 4; ++k) {
            if (k == 0) {
                out.emplace_back("morph", names[0], 0, 100, 50, [](double x) {
                    char b[32];
                    std::snprintf(b, sizeof b, "+%.1f st", x / 100 * syncSt);
                    return std::string(b);
                });
            } else {
                out.emplace_back("morph", names[size_t(k)], 0, 100, 50, Fmt::pct);
            }
        }
        return out;
    }();
    return v;
}

/// WAVE B на октаву ниже: пункт и под ним «−1».
static const ParamDef& waveBLow() {
    static const ParamDef p = [] {
        static const std::vector<std::string> o = {"Sine", "Triangle", "Saw", "Square"};
        return ParamDef("timbre", "Wave B", 0, 100, 50,
                        [](double v) { return o[size_t(Params::stepIndex(v / 100, int(o.size())))] + "\n−" "1"; })
            .withKind(KnobKind::stepped)
            .withSteps(int(o.size()));
    }();
    return p;
}

static double get(const ParamMap& m, const char* id) {
    const auto it = m.find(id);
    return it == m.end() ? 0 : it->second;
}

const ParamDef& knob(const ParamDef& p, int engine, const ParamMap& params) {
    if (engine != J::analog) return p;
    if (p.id == "timbre" && get(params, octId) >= 0.5) return waveBLow();
    if (p.id == "morph") {
        const int m = mode(get(params, modId));
        if (m > 0) return amount()[size_t(m - 1)];
    }
    return p;
}

}  // namespace AnalogMod
}  // namespace jambo
