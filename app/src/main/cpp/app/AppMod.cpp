// App: источники LFO1–3 и ENV — назначение на ручки дорожки (порт части AppModelMod.swift и KnobGrid.swift, ветка
// swift JAMbo). Удерживать источник и вращать ручку — степень −100…+100 %; на ручку — один источник; снять —
// удерживать тот же источник и дважды тапнуть по ручке. В MORPH — без UNDO.
#include "App.h"
#include <algorithm>

namespace jambo {

std::optional<int> App::heldSource() const {
    if (heldSources_.count(lastSource_)) return lastSource_;
    if (heldSources_.empty()) return std::nullopt;
    return *heldSources_.begin();
}

std::optional<int> App::assignSource() const {
    if (!held_.empty() || !canAssign()) return std::nullopt;
    return heldSource();
}

void App::setSource(int s, bool on, double now) {
    if (on) {
        heldSources_.insert(s);
        lastSource_ = s;
        sourceTapped(s, now);
    } else {
        heldSources_.erase(s);
    }
    bump();
}

/// Палец держит ручку с назначением: тап по другому источнику — назначение переходит к нему с той же степенью,
/// двойной тап по назначенному источнику — назначение снимается.
void App::sourceTapped(int s, double now) {
    const bool dbl = lastSourceTap_ && lastSourceTap_->first == s && now - lastSourceTap_->second < 0.35;
    if (dbl) lastSourceTap_.reset();
    else lastSourceTap_ = std::make_pair(s, now);
    for (int slot : knobFingers_) {
        const auto a = assign(slot);
        if (!a) continue;
        if (a->lfo != s) setAssign(slot, s, a->amount);
        else if (dbl) clearAssign(slot, s);
    }
}

void App::knobFinger(int slot, bool on) {
    if (channel_ == Channel::mix) return;
    if (on) knobFingers_.insert(slot);
    else knobFingers_.erase(slot);
    touchArrangeAuto(slot, on);
    bump();
}

std::optional<ModAssign> App::assign(int slot) const {
    if (!canAssign()) return std::nullopt;
    const ParamDef* p = param(slot);
    if (!p) return std::nullopt;
    const auto& as = fxMode() ? project_.fxAssigns : track().assigns;
    const auto it = as.find(p->id);
    if (it == as.end()) return std::nullopt;
    return it->second;
}

void App::setAssign(int slot, int lfo, double amount) {
    if (!canAssign()) return;
    const ParamDef* p = param(slot);
    if (!p) return;
    const double am = std::min(1.0, std::max(-1.0, amount));
    const std::string id = p->id;
    if (fxMode()) {
        // FX — общий: источник — LFO/ENV дорожки, выбранной при назначении.
        const ModAssign a(lfo, am, selected_);
        edit([&](Project& pr) { pr.fxAssigns[id] = a; });
        return;
    }
    const ModAssign a(lfo, am);
    if (morph_) {
        const int i = selected_;
        editMorph([&](Project& pr) {
            pr.tracks[size_t(i)].morphAssign(id);
            pr.tracks[size_t(i)].assigns[id] = a;
        });
        return;
    }
    editTrack([&](Track& t) { t.assigns[id] = a; });
}

void App::clearAssign(int slot, int lfo) {
    if (!canAssign()) return;
    const ParamDef* p = param(slot);
    if (!p) return;
    const std::string id = p->id;
    if (fxMode()) {
        const auto it = project_.fxAssigns.find(id);
        if (it != project_.fxAssigns.end() && it->second.lfo == lfo) edit([&](Project& pr) { pr.fxAssigns.erase(id); });
        return;
    }
    const auto& as = track().assigns;
    const auto it = as.find(id);
    if (it == as.end() || it->second.lfo != lfo) return;
    if (morph_) {
        const int i = selected_;
        editMorph([&](Project& pr) {
            pr.tracks[size_t(i)].morphAssign(id);
            pr.tracks[size_t(i)].assigns.erase(id);
        });
        return;
    }
    editTrack([&](Track& t) { t.assigns.erase(id); });
}

double App::assignStart(int slot, int lfo) const {
    const auto a = assign(slot);
    return a && a->lfo == lfo ? a->amount : 0;
}

std::pair<double, double> App::sourceRange(const ModAssign& a) const {
    if (a.lfo == 4) return {0, 1};
    const int t = a.track.value_or(selected_);
    if (t < 0 || t >= int(project_.tracks.size())) return {-1, 1};
    const auto& p = project_.tracks[size_t(t)].params;
    const auto get = [&](const std::string& id) {
        const auto it = p.find(id);
        return it == p.end() ? 0.0 : it->second;
    };
    const std::string n = std::to_string(a.lfo);
    const double u = std::min(1.0, std::max(0.0, get("lfo" + n + ".polar")));
    const double inv = get("lfo" + n + ".invert");
    const double lo = u - 1;
    if (inv >= 0.5) return {-1, -lo};
    return {lo, 1};
}

double App::modSource(int t, int lfo) const {
    return audio_->modOut(t * 4 + std::max(0, std::min(3, lfo - 1)));
}

std::optional<double> App::modulatedNorm(int slot) const {
    const auto a = assign(slot);
    if (!a) return std::nullopt;
    const KnobState st = knobState(slot);
    const int t = a->track.value_or(selected_);
    return std::min(1.0, std::max(0.0, st.norm + a->amount * modSource(t, a->lfo)));
}

bool App::anyAssign() const {
    if (!canAssign()) return false;
    for (int k = 0; k < 16; ++k)
        if (assign(k)) return true;
    return false;
}

std::set<int> App::fingerSources() const {
    std::set<int> out;
    for (int s : knobFingers_)
        if (auto a = assign(s)) out.insert(a->lfo);
    return out;
}

}  // namespace jambo
