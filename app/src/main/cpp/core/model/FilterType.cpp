// Порт Core/Model/FilterType.swift (ветка swift JAMbo): типы и перевод значений.
#include "FilterType.h"
#include <cmath>
#include "SynthEngines.h"
#include "../synth/Jambo.h"

namespace jambo {
namespace FilterType {

const std::vector<std::string> names = {"LP", "HP", "BP", "LAD"};

int of(const double* v) {
    if (!v || !std::isfinite(*v)) return lp;
    const double r = std::round(*v);
    return r >= 0 && r <= ladder ? int(r) : lp;
}

int effective(int type, const std::string& sound) {
    if (type == ladder && SynthKey::engine(sound) != J::sub) return lp;
    return type;
}

int next(int type, const std::string& sound, bool back) {
    const bool isSub = SynthKey::engine(sound) == J::sub;
    const int order[3] = {lp, isSub ? ladder : hp, bp};
    const int cur = effective(type, sound);
    int i = -1;
    for (int k = 0; k < 3; ++k) if (order[k] == cur) { i = k; break; }
    if (i < 0) return order[0];
    return order[(i + (back ? 2 : 1)) % 3];
}

double valueAfterSwitch(double v, int from, int to) {
    if (Fmt::filterOpen(v, from)) return Params::filterDef(to).def;
    return v;
}

}  // namespace FilterType

namespace FilterMigration {

int oldType(double v) {
    if (std::fabs(v) < 0.5) return -1;
    return v < 0 ? FilterType::lp : FilterType::hp;
}

double value(double v, int type) {
    const int t = oldType(v);
    if (t < 0 || t != type) return Params::filterDef(type).def;
    // LP: 20000·(30/20000)^(−v/100) Гц, HP: 20·500^(v/100) Гц → 100·log1000(Гц / 20).
    const double n = t == FilterType::lp ? 100 + v * std::log(20000.0 / 30) / std::log(1000.0)
                                         : v * std::log(500.0) / std::log(1000.0);
    return std::min(100.0, std::max(0.0, n));
}

int soundType(const double* base, const std::vector<double>& others) {
    if (base) {
        const int t = oldType(*base);
        if (t >= 0) return t;
    }
    int nlp = 0, nhp = 0;
    for (double v : others) {
        const int t = oldType(v);
        if (t == FilterType::lp) ++nlp;
        else if (t == FilterType::hp) ++nhp;
    }
    return nhp > nlp ? FilterType::hp : FilterType::lp;
}

void params(ParamMap& p, int type) {
    p[Params::filterTypeId] = double(type);
    // Нет ручки — прежний центр (выкл.): открытый фильтр своего типа.
    const auto it = p.find("filter");
    p["filter"] = value(it != p.end() ? it->second : 0, type);
}

void preset(ParamMap& p) {
    const auto it = p.find("filter");
    if (it == p.end()) return;
    const double v = it->second;
    params(p, soundType(&v, {}));
}

}  // namespace FilterMigration
}  // namespace jambo
