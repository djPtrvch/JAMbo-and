// MORPH: ручки на все дорожки и плавный возврат после выключения режима (порт Core/Model/MorphModes.swift и
// MORPH эффектов из Core/Model/TrackFx.swift, ветка swift JAMbo).
#include "Project.h"
#include "Params.h"
#include <algorithm>

namespace jambo {

namespace MorphReturn {
const std::vector<double> beats = {0, 1, 2, 4, 8};
const std::vector<std::string> names = {"OFF", "1/4", "1/2", "1 BAR", "2 BARS"};
double seconds(int k, double bpm) {
    if (k < 0 || k >= int(beats.size())) return 0;
    return beats[size_t(k)] * 60 / std::max(1.0, bpm);
}
}  // namespace MorphReturn

static const std::string morphFxKey = "fx:";

static double paramOr(const ParamMap& m, const std::string& id, double def) {
    const auto it = m.find(id);
    return it == m.end() ? def : it->second;
}

// MARK: — эффекты дорожки (TrackFx.swift)

void Project::morphFx(int t, const std::string& id) {
    if (t < 0 || t >= int(tracks.size())) return;
    tracks[size_t(t)].morphEdited = true;
    const std::string key = morphFxKey + id;
    if (!tracks[size_t(t)].morphOrig.count(key)) tracks[size_t(t)].morphOrig[key] = fxValue(t, id);
}

void Project::morphShiftFx(int sel, const ParamDef& p, double norm) {
    if (sel < 0 || sel >= int(tracks.size())) return;
    const std::string key = morphFxKey + p.id;
    const double o0 = paramOr(tracks[size_t(sel)].morphOrig, key, fxValue(sel, p.id));
    const double delta = norm - p.toNorm(o0);
    for (int i = 0; i < int(tracks.size()); ++i) {
        if (i == sel || fxClip(i) < 0) continue;
        morphFx(i, p.id);
        const double o = paramOr(tracks[size_t(i)].morphOrig, key, fxValue(i, p.id));
        const double n = std::min(1.0, std::max(0.0, p.toNorm(o) + delta));
        setFx(i, p.id, p.fromNorm(n));
    }
}

void Project::undoMorph(int i) {
    if (i < 0 || i >= int(tracks.size())) return;
    for (const auto& [key, v] : tracks[size_t(i)].morphOrig)
        if (auto id = morphFxId(key)) setFx(i, *id, v);
    tracks[size_t(i)].undoMorph();
}

std::optional<double> Project::morphValue(int i, const std::string& key) const {
    if (auto id = morphFxId(key)) return fxValue(i, *id);
    const auto& pm = tracks[size_t(i)].params;
    const auto it = pm.find(key);
    if (it == pm.end()) return std::nullopt;
    return it->second;
}

void Project::setMorphValue(int i, const std::string& key, double v) {
    if (auto id = morphFxId(key)) setFx(i, *id, v);
    else tracks[size_t(i)].params[key] = v;
}

// MARK: — MorphModes.swift

void Project::morphShift(int sel, const ParamDef& p0, double norm) {
    const std::string id = p0.id;
    if (sel < 0 || sel >= int(tracks.size())) return;
    const Track& s = tracks[size_t(sel)];
    const double o0 = paramOr(s.morphOrig, id, paramOr(s.params, id, p0.def));
    const double delta = norm - p0.toNorm(o0);
    for (int i = 0; i < int(tracks.size()); ++i) {
        if (i == sel) continue;
        const ParamDef* p = nullptr;
        for (const auto& d : Params::grid(tracks[size_t(i)].sample))
            if (!d.id.empty() && d.id == id) { p = &d; break; }
        if (!p)
            for (const auto& d : Params::modGrid())
                if (!d.id.empty() && d.id == id) { p = &d; break; }
        if (!p) continue;
        tracks[size_t(i)].morphParam(id);
        const double o = paramOr(tracks[size_t(i)].morphOrig, id, p->def);
        const double n = std::min(1.0, std::max(0.0, p->toNorm(o) + delta));
        tracks[size_t(i)].params[id] = p->fromNorm(n);
    }
}

void Project::keepMorph(const Project& cur) {
    for (int i = 0; i < int(tracks.size()); ++i) {
        const auto ci = std::find_if(cur.tracks.begin(), cur.tracks.end(),
                                     [&](const Track& t) { return t.id == tracks[size_t(i)].id; });
        if (ci == cur.tracks.end()) continue;
        const int cix = int(ci - cur.tracks.begin());
        const Track& c = *ci;
        // Эффекты (ключи «fx:»): исходное — снимка без его MORPH, сейчас — как у текущего.
        ParamMap fxOrig;
        for (const auto& [key, v] : c.morphOrig) {
            (void)v;
            if (!morphFxId(key)) continue;
            const auto it = tracks[size_t(i)].morphOrig.find(key);
            fxOrig[key] = it != tracks[size_t(i)].morphOrig.end() ? it->second : morphValue(i, key).value_or(0);
        }
        const ParamMap own = tracks[size_t(i)].morphOrig;
        for (const auto& [key, b] : own)
            if (morphFxId(key) && !c.morphOrig.count(key)) setMorphValue(i, key, b);
        for (const auto& [key, v] : c.morphOrig) {
            (void)v;
            if (morphFxId(key))
                if (auto cv = cur.morphValue(cix, key)) setMorphValue(i, key, *cv);
        }
        tracks[size_t(i)].keepMorph(c);
        for (const auto& [key, v] : fxOrig) tracks[size_t(i)].morphOrig[key] = v;
    }
}

std::vector<MorphPoint> Project::morphReturnPoints() const {
    std::vector<MorphPoint> out;
    for (int i = 0; i < int(tracks.size()); ++i) {
        const Track& t = tracks[size_t(i)];
        if (!t.morphEdited) continue;
        for (const auto& [id, to] : t.morphOrig) {
            const double from = morphValue(i, id).value_or(to);
            if (from != to) out.push_back({t.id, id, from, to});
        }
    }
    return out;
}

void Project::applyMorphReturn(const std::vector<MorphPoint>& pts, double f) {
    const double k = std::min(1.0, std::max(0.0, f));
    for (const auto& pt : pts) {
        const std::string pid = morphFxId(pt.param).value_or(pt.param);
        const auto it = std::find_if(tracks.begin(), tracks.end(), [&](const Track& t) { return t.id == pt.track; });
        const ParamDef* p = Params::byId(pid);
        if (it == tracks.end() || !p) continue;
        const double a = p->toNorm(pt.from);
        const double b = p->toNorm(pt.to);
        setMorphValue(int(it - tracks.begin()), pt.param, k >= 1 ? pt.to : p->fromNorm(a + (b - a) * k));
    }
}

void Track::keepMorph(const Track& cur) {
    ParamMap orig;
    for (const auto& [id, v] : cur.morphOrig) {
        (void)v;
        if (Project::morphFxId(id)) continue;
        const auto it = morphOrig.find(id);
        if (it != morphOrig.end()) { orig[id] = it->second; continue; }
        const auto pt = params.find(id);
        if (pt != params.end()) { orig[id] = pt->second; continue; }
        const ParamDef* d = Params::byId(id);
        orig[id] = d ? d->def : 0;
    }
    for (const auto& [id, b] : morphOrig)
        if (!cur.morphOrig.count(id) && !Project::morphFxId(id)) params[id] = b;
    for (const auto& [id, v] : cur.morphOrig) {
        (void)v;
        if (Project::morphFxId(id)) continue;
        // Swift: params[id] = cur.params[id] — нет у cur — ключ убирается.
        const auto it = cur.params.find(id);
        if (it != cur.params.end()) params[id] = it->second;
        else params.erase(id);
    }
    std::map<std::string, std::optional<ModAssign>> origA;
    for (const auto& [id, a] : cur.morphOrigAssign) {
        (void)a;
        const auto it = morphOrigAssign.find(id);
        if (it != morphOrigAssign.end()) {
            origA[id] = it->second;
        } else {
            const auto as = assigns.find(id);
            origA[id] = as != assigns.end() ? std::optional<ModAssign>(as->second) : std::nullopt;
        }
    }
    for (const auto& [id, a] : morphOrigAssign) {
        if (cur.morphOrigAssign.count(id)) continue;
        if (a) assigns[id] = *a;
        else assigns.erase(id);
    }
    for (const auto& [id, a] : cur.morphOrigAssign) {
        (void)a;
        const auto it = cur.assigns.find(id);
        if (it != cur.assigns.end()) assigns[id] = it->second;
        else assigns.erase(id);
    }
    morphOrig = orig;
    morphOrigAssign = origA;
    morphEdited = cur.morphEdited;
}

}  // namespace jambo
