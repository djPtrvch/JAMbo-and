// Порт Core/Model/SynthMigration.swift и GranularMigration.swift (ветка swift JAMbo): версии 4 и 6.
#include "SynthMigration.h"
#include <cmath>
#include "Project.h"
#include "SynthEngines.h"

namespace jambo {

static double get(const ParamMap& p, const char* k, double def) {
    const auto it = p.find(k);
    return it != p.end() ? it->second : def;
}

namespace SynthMigration {

bool params(const std::string& sound, ParamMap& p) {
    const int e = SynthKey::engine(sound);
    if (e < 0) return false;
    const MigratedKnobs r = migrateKnobs(e, get(p, "harm", 50), get(p, "timbre", 50), get(p, "morph", 50));
    p["harm"] = r.h;
    p["timbre"] = r.t;
    p["morph"] = r.m;
    p["p4"] = r.p4;
    return r.lostSub;
}

void locks(int e, ParamMap& l) {
    const bool has0 = l.count("harm"), has1 = l.count("timbre"), has2 = l.count("morph");
    if (!has0 && !has1 && !has2) return;
    const MigratedKnobs r = migrateKnobs(e, get(l, "harm", 0), get(l, "timbre", 0), get(l, "morph", 0));
    switch (e) {
    case J::drum:
        if (has2) l["p4"] = r.p4;
        break;
    case J::analog:
        if (has0) l["timbre"] = r.t; else l.erase("timbre");
        if (has1) l["morph"] = r.m; else l.erase("morph");
        break;
    case J::wtable:
        if (has2) {
            l["morph"] = r.m;
            l["p4"] = r.p4;
        }
        break;
    default:
        break;
    }
}

void assigns(int e, std::map<std::string, ModAssign>& a) {
    switch (e) {
    case J::drum: {
        const auto m = a.find("morph");
        if (m != a.end()) a["p4"] = m->second;
        break;
    }
    case J::analog: {
        const auto t = a.find("timbre");
        if (t != a.end()) a["morph"] = t->second; else a.erase("morph");
        a.erase("timbre");
        break;
    }
    case J::wtable: {
        const auto m = a.find("morph");
        if (m != a.end()) a["p4"] = m->second; else a.erase("p4");
        a.erase("morph");
        break;
    }
    default:
        break;
    }
}

}  // namespace SynthMigration

void Project::migrateSynthKnobs() {
    std::vector<std::string> lost;
    for (auto& t : tracks) {
        const int e = SynthKey::engine(t.sample);
        if (SynthMigration::params(t.sample, t.params)) lost.push_back(t.name);
        if (e >= 0) SynthMigration::assigns(e, t.assigns);
        if (t.home) {
            SynthMigration::params(t.home->sample, t.home->params);
            const int he = SynthKey::engine(t.home->sample);
            if (he >= 0) SynthMigration::assigns(he, t.home->assigns);
        }
        for (auto& oc : t.clips) {
            if (!oc) continue;
            Clip& c = *oc;
            if (c.src) {
                SynthMigration::params(c.src->sample, c.src->params);
                const int se = SynthKey::engine(c.src->sample);
                if (se >= 0) SynthMigration::assigns(se, c.src->assigns);
            }
            for (auto& page : c.pages)
                for (auto& s : page) {
                    const int se = SynthKey::engine(s.sound ? *s.sound : t.sample);
                    if (se >= 0) SynthMigration::locks(se, s.locks);
                }
        }
    }
    if (!lost.empty()) {
        std::string n = "SUB removed (ANALOG): ";
        for (size_t i = 0; i < lost.size(); ++i) n += (i ? ", " : "") + lost[i];
        notice = n;
    }
}

// MARK: — GRANULAR (версия 6)

namespace GranularMigration {

Knobs knobs(double s, double d, double p) {
    const double sec = 0.005 * std::pow(100.0, s / 100);
    const double rate = 2 * std::pow(40.0, d / 100);
    const double ns = 100 * std::log(std::max(0.01, sec) / 0.01) / std::log(80.0);
    const double nd = 100 * std::log(std::max(0.5, rate * sec) / 0.5) / std::log(48.0);
    const double np = 100 * std::sqrt(1.75 * std::max(0.0, p) / 100);
    auto c = [](double v) { return std::min(100.0, std::max(0.0, v)); };
    return {c(ns), c(nd), c(np)};
}

void params(const std::string& sound, ParamMap& p) {
    if (SynthKey::engine(sound) != J::granular) return;
    const Knobs r = knobs(get(p, "timbre", 50), get(p, "morph", 50), get(p, "p4", 50));
    if (p.count("timbre")) p["timbre"] = r.s;
    if (p.count("morph")) p["morph"] = r.d;
    if (p.count("p4")) p["p4"] = r.p;
}

static void locks(ParamMap& l, const ParamMap& base) {
    const bool hs = l.count("timbre"), hd = l.count("morph"), hp = l.count("p4");
    if (!hs && !hd && !hp) return;
    const Knobs r = knobs(hs ? l.at("timbre") : get(base, "timbre", 50), get(l, "morph", 50), get(l, "p4", 0));
    if (hs) l["timbre"] = r.s;
    if (hd) l["morph"] = r.d;
    if (hp) l["p4"] = r.p;
}

static void lanes(std::vector<ArrAutoLane>& ls, const ParamMap& base) {
    for (auto& l : ls) {
        const std::string& id = l.param;
        if (id != "timbre" && id != "morph" && id != "p4") continue;
        for (auto& pt : l.points) {
            const double v = pt.y * 100;
            const Knobs r = knobs(id == "timbre" ? v : get(base, "timbre", 50), v, v);
            const double n = id == "timbre" ? r.s : (id == "morph" ? r.d : r.p);
            pt.y = n / 100;
        }
    }
}

static void clip(Clip& c, const std::string& sample, const ParamMap& base) {
    for (auto& page : c.pages)
        for (auto& s : page)
            if (SynthKey::engine(s.sound ? *s.sound : sample) == J::granular) locks(s.locks, base);
}

}  // namespace GranularMigration

void Project::migrateGranular() {
    using namespace GranularMigration;
    for (auto& t : tracks) {
        const ParamMap base = t.params;
        for (auto& oc : t.clips) {
            if (!oc) continue;
            Clip& c = *oc;
            const std::string snd = c.src ? c.src->sample : t.sample;
            const ParamMap b = c.src ? c.src->params : base;
            clip(c, snd, b);
            if (c.src) GranularMigration::params(c.src->sample, c.src->params);
        }
        const bool gran = SynthKey::engine(t.sample) == J::granular;
        for (auto& blk : t.blocks) {
            if (blk.pattern) clip(*blk.pattern, t.sample, base);
            if (gran) lanes(blk.autos, base);
        }
        if (gran) {
            lanes(t.autos, base);
            // MORPH: исходные ручки — в тех же единицах.
            ParamMap& mo = t.morphOrig;
            const bool h0 = mo.count("timbre"), h1 = mo.count("morph"), h2 = mo.count("p4");
            ParamMap full = base;
            for (const auto& kv : mo) full[kv.first] = kv.second;
            GranularMigration::params(t.sample, full);
            if (h0) mo["timbre"] = full["timbre"];
            if (h1) mo["morph"] = full["morph"];
            if (h2) mo["p4"] = full["p4"];
        }
        GranularMigration::params(t.sample, t.params);
        if (t.home) GranularMigration::params(t.home->sample, t.home->params);
    }
}

}  // namespace jambo
