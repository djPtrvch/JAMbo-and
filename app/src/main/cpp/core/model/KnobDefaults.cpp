// Порт Core/Model/KnobDefaults.swift (ветка swift JAMbo), без JSON.
#include "KnobDefaults.h"
#include "FilterType.h"
#include "Project.h"
#include "SynthEngines.h"

namespace jambo {

std::string KnobDefaults::soundScope(const std::string& sound) {
    const int e = SynthKey::engine(sound);
    return e >= 0 ? "synth" + std::to_string(e) : "sampler";
}

std::string KnobDefaults::companion(const std::string& id) {
    if (id == "dly.time") return "dly.sync";
    if (id == "rate") return Params::rateModeId;
    if (id == "filter") return Params::filterTypeId;
    for (size_t l = 0; l < Params::lfoSyncIds.size(); ++l)
        if (id == "lfo" + std::to_string(l + 1) + ".speed") return Params::lfoSyncIds[l];
    return "";
}

std::optional<double> KnobDefaults::value(const std::string& scope, const std::string& id) const {
    const auto it = values.find(key(scope, id));
    if (it == values.end()) return std::nullopt;
    return it->second;
}

void KnobDefaults::apply(Track& t) const {
    applySound(t);
    std::vector<std::string> ids = Params::modIds();
    ids.insert(ids.end(), Params::lfoSyncIds.begin(), Params::lfoSyncIds.end());
    for (const auto& id : ids)
        if (auto v = value("mod", id)) t.params[id] = *v;
    if (eqTrack) t.eq.apply(*eqTrack, 0);
}

void KnobDefaults::applySound(Track& t) const {
    const std::string scope = soundScope(t.sample);
    for (const auto& p : Params::grid(t.sample)) {
        if (p.id.empty()) continue;
        if (auto v = value(scope, p.id)) t.params[p.id] = *v;
        if (p.id == "rate")
            if (auto m = value(scope, Params::rateModeId)) t.params[Params::rateModeId] = *m;
        if (p.id == "filter")
            if (auto m = value(scope, Params::filterTypeId)) t.params[Params::filterTypeId] = *m;
    }
}

void KnobDefaults::apply(Project& p) const {
    for (const auto& id : Params::fxIds)
        if (auto v = value("fx", id)) p.fx[id] = *v;
    // Ручки MST и параметры модулей мастера.
    std::vector<std::string> ids;
    for (const auto& d : Params::masterGrid()) ids.push_back(d.id);
    for (const auto& m : MasterChain::ids()) ids.push_back(m.first);
    for (const auto& id : ids) {
        if (id.empty()) continue;
        if (auto v = value("mst", id)) p.master[id] = *v;
    }
    if (eqMaster) p.masterEq.apply(*eqMaster, 0);
    for (auto& t : p.tracks) apply(t);
}

void KnobDefaults::migrateFilter() {
    // Swift правит словарь, обходя его копию: новые ключи «…/filter.type» в обход не попадают.
    const ParamMap old = values;
    const std::string suffix = "/filter";
    for (const auto& kv : old) {
        const std::string& k = kv.first;
        if (k.size() < suffix.size() || k.compare(k.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const double v = kv.second;
        const int type = FilterMigration::soundType(&v, {});
        values[k] = FilterMigration::value(v, type);
        values[k.substr(0, k.size() - 6) + Params::filterTypeId] = double(type);
    }
}

}  // namespace jambo
