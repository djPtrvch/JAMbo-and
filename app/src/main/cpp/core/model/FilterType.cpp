// Порт Core/Model/FilterType.swift (ветка swift JAMbo): типы и перевод значений.
#include "FilterType.h"
#include <algorithm>
#include <cmath>
#include "Project.h"
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

void locks(Step& s, int type) {
    const auto it = s.locks.find("filter");
    if (it == s.locks.end()) return;
    const double v = it->second;
    if (s.soundLocks) {
        std::vector<std::string>& sl = *s.soundLocks;
        if (std::find(sl.begin(), sl.end(), "filter") != sl.end()) {
            const int own = soundType(&v, {});
            s.locks["filter"] = value(v, own);
            s.locks[Params::filterTypeId] = double(own);
            if (std::find(sl.begin(), sl.end(), Params::filterTypeId) == sl.end()) sl.push_back(Params::filterTypeId);
            return;
        }
    }
    s.locks["filter"] = value(v, type);
}

void clip(Clip& c, int type) {
    for (auto& page : c.pages) for (auto& s : page) locks(s, type);
}

void lanes(std::vector<ArrAutoLane>& ls, int type) {
    for (auto& l : ls) {
        if (l.param != "filter") continue;
        for (auto& p : l.points) p.y = value(p.y * 200 - 100, type) / 100;
    }
}

std::vector<double> used(const Track& t) {
    std::vector<double> out;
    auto add = [&out](const Clip& c) {
        for (const auto& page : c.pages)
            for (const auto& s : page) {
                if (s.soundLocks && std::find(s.soundLocks->begin(), s.soundLocks->end(), "filter") != s.soundLocks->end())
                    continue;
                const auto it = s.locks.find("filter");
                if (it != s.locks.end()) out.push_back(it->second);
            }
    };
    for (const auto& c : t.clips) if (c && !c->src) add(*c);
    for (const auto& b : t.blocks) if (b.pattern) add(*b.pattern);
    std::vector<ArrAutoLane> ls = t.autos;
    for (const auto& b : t.blocks) ls.insert(ls.end(), b.autos.begin(), b.autos.end());
    for (const auto& l : ls)
        if (l.param == "filter") for (const auto& p : l.points) out.push_back(p.y * 200 - 100);
    return out;
}

}  // namespace FilterMigration

void Project::migrateFilter() {
    using namespace FilterMigration;
    for (auto& t : tracks) {
        const auto base = t.params.find("filter");
        const double bv = base != t.params.end() ? base->second : 0;
        const int type = soundType(base != t.params.end() ? &bv : nullptr, used(t));
        for (auto& oc : t.clips) {
            if (!oc) continue;
            Clip& c = *oc;
            if (c.src) {
                std::vector<double> others;
                for (const auto& page : c.pages)
                    for (const auto& s : page) {
                        const auto it = s.locks.find("filter");
                        if (it != s.locks.end()) others.push_back(it->second);
                    }
                const auto sf = c.src->params.find("filter");
                const double sv = sf != c.src->params.end() ? sf->second : 0;
                const int st = soundType(sf != c.src->params.end() ? &sv : nullptr, others);
                params(c.src->params, st);
                clip(c, st);
            } else {
                clip(c, type);
            }
        }
        for (auto& b : t.blocks) {
            if (b.pattern) clip(*b.pattern, type);
            lanes(b.autos, type);
        }
        lanes(t.autos, type);
        const auto mo = t.morphOrig.find("filter");
        if (mo != t.morphOrig.end()) mo->second = value(mo->second, type);
        if (t.home) {
            const auto hf = t.home->params.find("filter");
            const double hv = hf != t.home->params.end() ? hf->second : 0;
            params(t.home->params, soundType(hf != t.home->params.end() ? &hv : nullptr, {}));
        }
        params(t.params, type);
    }
}

}  // namespace jambo
