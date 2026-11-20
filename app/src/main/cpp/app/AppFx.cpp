// App: страницы FX и MOD, SHIFT (порт AppModelFx.swift, части AppModelMod.swift, AppModelGrid.swift и
// AppModelDefaults.swift, ветка swift JAMbo). FX — общие эффекты (Reverb, Delay, Drive — у клипа дорожки, Duck — у
// клипа-лидера); MOD — LFO1–3 и ENV дорожки. Без MORPH, своих значений по умолчанию и слежения ударных за нотами.
#include "App.h"
#include "model/AnalogMod.h"
#include "model/FilterType.h"
#include "model/KnobDefaults.h"
#include "model/SynthEngines.h"
#include "synth/Jambo.h"
#include <cmath>

namespace jambo {

bool App::fxMode() const { return fxHeld_ && channel_ == Channel::track && (!modHeld_ || lastPage_ == GridPage::fx); }

bool App::modMode() const { return modHeld_ && channel_ == Channel::track && (!fxHeld_ || lastPage_ == GridPage::mod); }

void App::setPageHeld(GridPage p, bool on) {
    if (p == GridPage::fx) fxHeld_ = on;
    else modHeld_ = on;
    if (on) lastPage_ = p;
    bump();
}

void App::setLatched(bool fx, bool mod) {
    latchFx_ = fx;
    latchMod_ = mod;
    bump();
}

void App::pagePress(GridPage p) {
    if (knobShift()) {
        const bool on = !latched(p);
        if (p == GridPage::fx) latchFx_ = on;
        else latchMod_ = on;
        if (!on) setPageHeld(p, false);
        bump();
        return;
    }
    if (latched(p)) {
        const bool on = !pageOn(p);
        setPageHeld(p, on);
        const GridPage other = p == GridPage::fx ? GridPage::mod : GridPage::fx;
        if (on && latched(other) && pageOn(other)) setPageHeld(other, false);
        return;
    }
    setPageHeld(p, true);
}

void App::pageRelease(GridPage p) {
    if (!latched(p)) setPageHeld(p, false);
}


// MARK: — параметры FX и MOD

const ParamDef& App::fxParam(int slot) const {
    const ParamDef& p = Params::fxGrid()[size_t(slot)];
    if (p.id == "dly.time" && !delaySynced()) return Params::dlyTimeFree();
    if (p.id != "sc.source") return p;
    std::vector<std::string> names;
    for (const auto& t : project_.tracks) names.push_back(t.name);
    const double mx = double(std::max(0, int(names.size()) - 1));
    dynParams_[size_t(slot)] = ParamDef(p.id, p.label, 0, mx, 0, [names](double v) {
                                   const int i = int(std::lround(v));
                                   return i >= 0 && i < int(names.size()) ? names[size_t(i)] : std::string();
                               })
                                   .withKind(KnobKind::stepped)
                                   .withOptions(names);
    return dynParams_[size_t(slot)];
}

const ParamDef& App::modParam(int slot) const {
    const ParamDef& p = Params::modGrid()[size_t(slot)];
    const int l = lfoSpeedIndex(slot);
    if (l >= 0 && lfoSynced(l)) {
        dynParams_[size_t(slot)] = Params::lfoSpeedSync(l + 1);
        return dynParams_[size_t(slot)];
    }
    return p;
}

double App::baseValue(const ParamDef& p) const {
    if (fxMode()) return project_.fxValue(selected_, p.id);
    const auto& pm = track().params;
    const auto it = pm.find(p.id);
    return it == pm.end() ? p.def : it->second;
}

void App::setFx(const ParamDef& p, double v) {
    const int i = selected_;
    const std::string id = p.id;
    edit([&](Project& pr) {
        pr.setFx(i, id, v);
        if (!Project::trackFx(id)) writeLead(pr);
    });
}

void App::writeLead(Project& p) const {
    const int i = std::min(lead_, int(p.tracks.size()) - 1);
    for (int t = 0; t < int(p.tracks.size()); ++t) {
        if (t != i && p.tracks[size_t(t)].stopped) continue;
        const int k = p.tracks[size_t(t)].slot;
        // Клипы PREV (переход MIX) — с переходом.
        auto& c = p.tracks[size_t(t)].clips[size_t(k)];
        if (!c) continue;
        c->scale = p.scale;
        c->root = p.root;
    }
    const int k = p.tracks[size_t(i)].slot;
    auto& c = p.tracks[size_t(i)].clips[size_t(k)];
    if (!c) return;
    ParamMap f = c->fx.value_or(ParamMap{});
    for (const auto& [id, v] : p.fx)
        if (!Project::trackFx(id)) f[id] = v;
    c->fx = f;
}

bool App::delaySynced() const { return project_.fxValue(selected_, "dly.sync") >= 0.5; }

bool App::lfoSynced(int l) const {
    const auto& pm = track().params;
    const auto it = pm.find(Params::lfoSyncIds[size_t(l)]);
    return it != pm.end() && it->second >= 0.5;
}

int App::lfoSpeedIndex(int slot) const {
    const auto& g = Params::modGrid();
    if (slot < 0 || slot >= int(g.size())) return -1;
    const std::string& id = g[size_t(slot)].id;
    for (int l = 0; l < 3; ++l)
        if (id == "lfo" + std::to_string(l + 1) + ".speed") return l;
    return -1;
}

bool App::rateStretch() const {
    const auto& pm = track().params;
    const auto it = pm.find(Params::rateModeId);
    return it != pm.end() && it->second >= 0.5;
}

int App::filterType() const {
    const auto& pm = track().params;
    const auto it = pm.find(Params::filterTypeId);
    return FilterType::effective(it == pm.end() ? FilterType::of(nullptr) : FilterType::of(it->second), track().sample);
}

// MARK: — SHIFT + тап

void App::filterShiftTap(bool undo) {
    const int from = filterType();
    const int to = FilterType::next(from, track().sample, undo);
    const auto& pm = track().params;
    const auto it = pm.find("filter");
    const double cur = it == pm.end() ? Params::filterDef(from).def : it->second;
    const double v = FilterType::valueAfterSwitch(cur, from, to);
    editTrack([&](Track& t) {
        t.params[Params::filterTypeId] = double(to);
        t.params["filter"] = v;
    });
}

bool App::analogShiftTap(const std::string& id, bool undo) {
    const auto& pm = track().params;
    const auto get = [&](const char* k) {
        const auto it = pm.find(k);
        return it == pm.end() ? 0.0 : it->second;
    };
    const double mod = get(AnalogMod::modId);
    if (id == "harm") {
        const double v = AnalogMod::toggled(mod);
        editTrack([&](Track& t) { t.params[AnalogMod::modId] = v; });
        return true;
    }
    if (id == "timbre") {
        const bool on = get(AnalogMod::octId) < 0.5;
        editTrack([&](Track& t) { t.params[AnalogMod::octId] = on ? 1 : 0; });
        return true;
    }
    if (id == "morph" && AnalogMod::mode(mod) > 0) {
        const double v = AnalogMod::cycled(mod, undo);
        editTrack([&](Track& t) { t.params[AnalogMod::modId] = v; });
        return true;
    }
    return false;
}

bool App::knobShiftTap(int slot, bool undo) {
    if (channel_ != Channel::track) return false;
    const ParamDef* p = param(slot);
    if (!p) return false;
    const std::string id = p->id;
    const bool plain = !modMode() && !fxMode();
    if (plain && SynthKey::engine(track().sample) == J::analog && analogShiftTap(id, undo)) return true;
    if (plain && id == "filter") {
        filterShiftTap(undo);
        return true;
    }
    if (plain && id == "rate") {
        const bool on = !rateStretch();
        editTrack([&](Track& t) { t.params[Params::rateModeId] = on ? 1 : 0; });
        return true;
    }
    if (modMode()) {
        const int l = lfoSpeedIndex(slot);
        if (l < 0) return false;
        const std::string sid = Params::lfoSyncIds[size_t(l)];
        const bool on = !lfoSynced(l);
        editTrack([&](Track& t) { t.params[sid] = on ? 1 : 0; });
        return true;
    }
    if (!fxMode() || Params::fxGrid()[size_t(slot)].id != "dly.time") return false;
    // Время сохраняется: по темпу — ближайший пункт, свободно — те же мс.
    const double v = baseValue(fxParam(slot));
    const double bpm = project_.bpm;
    const bool toSync = !delaySynced();
    double nv;
    if (toSync) {
        nv = double(Params::nearestDelayDiv(Params::dlyFreeMs(v), bpm));
    } else {
        const int k = std::min(int(Params::delayDivs.size()) - 1, std::max(0, int(std::lround(v))));
        nv = Params::dlyFreeValue(Params::delayDivs[size_t(k)].beats * 60000 / std::max(1.0, bpm));
    }
    const int i = selected_;
    edit([&](Project& pr) {
        pr.setFx(i, "dly.sync", toSync ? 1 : 0);
        pr.setFx(i, "dly.time", nv);
    });
    return true;
}

// MARK: — значение по умолчанию

void App::resetKnob(int slot) {
    const ParamDef* p = param(slot);
    if (!p || p->id.empty()) return;
    const std::string c = KnobDefaults::companion(p->id);
    double v = p->def;
    std::optional<double> cv;
    if (!c.empty()) {
        const ParamDef* cd = Params::byId(c);
        cv = cd ? cd->def : 0;
        // RATE: режим PTCH / TSTR сбрасывается только к своему по умолчанию (заводской сброс его не трогает).
        if (p->id == "rate") {
            const auto& pm = track().params;
            const auto it = pm.find(c);
            cv = it == pm.end() ? (cd ? cd->def : 0) : it->second;
        }
    }
    // Заводское Dly Time — пункт по темпу.
    if (p->id == "dly.time" && cv == 1.0 && v != std::round(v)) v = std::round(v);
    const int i = selected_;
    const std::string id = p->id;
    if (channel_ == Channel::master) {
        edit([&](Project& pr) { pr.master[id] = v; });
    } else if (fxMode()) {
        edit([&](Project& pr) {
            pr.setFx(i, id, v);
            if (!c.empty() && cv) pr.setFx(i, c, *cv);
            if (!Project::trackFx(id)) writeLead(pr);
        });
    } else {
        edit([&](Project& pr) {
            pr.tracks[size_t(i)].params[id] = v;
            if (!c.empty() && cv) pr.tracks[size_t(i)].params[c] = *cv;
        });
    }
}

}  // namespace jambo
