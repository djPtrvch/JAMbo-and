// Порт Core/Model/TrackFx.swift (ветка swift JAMbo): Reverb, Delay, Drive — у дорожки, в её клипах; Duck — общий.
// MORPH эффектов — с правками.
#include "Project.h"

namespace jambo {

int Project::fxClip(int t) const {
    if (t < 0 || t >= int(tracks.size())) return -1;
    const Track& tr = tracks[size_t(t)];
    if (tr.slot >= 0 && tr.slot < int(tr.clips.size()) && tr.clips[size_t(tr.slot)]) return tr.slot;
    if (tr.open >= 0 && tr.open < int(tr.clips.size()) && tr.clips[size_t(tr.open)]) return tr.open;
    return -1;
}

static double commonFx(const ParamMap& fx, const std::string& id) {
    const auto it = fx.find(id);
    if (it != fx.end()) return it->second;
    const ParamDef* p = Params::byId(id);
    return p ? p->def : 0;
}

double Project::fxValue(int t, const std::string& id) const {
    const double common = commonFx(fx, id);
    if (!trackFx(id)) return common;
    const int k = fxClip(t);
    if (k < 0) return common;
    const auto& c = tracks[size_t(t)].clips[size_t(k)];
    if (!c || !c->fx) return common;
    const auto it = c->fx->find(id);
    return it != c->fx->end() ? it->second : common;
}

void Project::setFx(int t, const std::string& id, double v) {
    const int k = trackFx(id) ? fxClip(t) : -1;
    if (k < 0) {
        fx[id] = v;
        return;
    }
    auto& c = tracks[size_t(t)].clips[size_t(k)];
    if (!c->fx) c->fx = ParamMap();
    (*c->fx)[id] = v;
}

void Project::migrateTrackFx() {
    ParamMap common;
    for (const auto& id : Params::fxIds) if (trackFx(id)) common[id] = commonFx(fx, id);
    for (auto& t : tracks)
        for (auto& c : t.clips) {
            if (!c) continue;
            if (!c->fx) c->fx = ParamMap();
            for (const auto& kv : common) (*c->fx)[kv.first] = kv.second;
        }
}

// MORPH эффектов дорожки: исходное — в Track::morphOrig под ключом «fx:» + id.
std::optional<std::string> Project::morphFxId(const std::string& key) {
    static const std::string k = "fx:";
    if (key.compare(0, k.size(), k) != 0) return std::nullopt;
    return key.substr(k.size());
}

}  // namespace jambo
