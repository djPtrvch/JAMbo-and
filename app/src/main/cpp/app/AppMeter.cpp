// App: измеритель уровня справа (порт AppModelMeter.swift, ветка swift JAMbo): канал — выбранная дорожка, при MIX и
// MST — выход мастера; регулятор громкости поверх полос; стерео или моно.
#include "App.h"
#include "synth/Jambo.h"
#include "model/SynthEngines.h"
#include <cmath>

namespace jambo {

int App::meterChannel() const { return channel_ == Channel::track ? selected_ : Levels::master; }

void App::tickMeters(double now) {
    Levels* lv = audio_->levels();
    if (!lv) return;
    meters_.show(meterChannel(), *lv);
    meters_.tick(*lv, now);
}

float App::meterLufs() const {
    const Levels* lv = audio_->levels();
    return lv ? lv->loudness.integrated.load() : -INFINITY;
}

void App::resetMeterPeak() { meters_.resetPeak(meterChannel()); }

void App::resetLoudness() {
    Levels* lv = audio_->levels();
    if (lv) lv->loudness.resetRequests.store(lv->loudness.resetRequests.load() + 1);
}

double App::meterVol(int ch) const {
    if (ch == Levels::master) return project_.outVol;
    if (ch >= int(project_.tracks.size())) return Params::volDef;
    const Track& t = project_.tracks[size_t(ch)];
    if (!levelRelative_) return t.vol;
    if (t.trim <= Params::volMin + 0.05) return Params::volMin;
    return std::max(Params::volMin, t.vol + t.trim);
}

void App::setMeterVol(int ch, double db) {
    if (ch == Levels::master) {
        const double v = std::max(Params::volMin, std::min(0.0, db));
        edit([&](Project& p) { p.outVol = v; });
    } else if (ch >= 0 && ch < int(project_.tracks.size())) {
        if (levelRelative_) {
            // LEVEL RELATIVE: итоговая громкость (не выше громкости MIX) — подстройка trim.
            const double vol = project_.tracks[size_t(ch)].vol;
            const double total = std::max(Params::volMin, std::min(vol, db));
            const double trim = total <= Params::volMin + 0.05 ? Params::volMin
                                                               : std::max(Params::volMin, std::min(0.0, total - vol));
            edit([&](Project& p) { p.tracks[size_t(ch)].trim = trim; });
        } else {
            const double v = std::max(Params::volMin, std::min(0.0, db));
            edit([&](Project& p) { p.tracks[size_t(ch)].vol = v; });
        }
    }
}

void App::setVolDrag(bool on) {
    meters_.volDrag = on;
    if (on) beginGesture();
    else endGesture();
}

namespace {
/// Звук по ключу — стерео: движок TEXTURE, GRANULAR (стерео-сэмплы — с загрузкой сэмплов).
bool soundStereo(const std::string& key) {
    if (key.empty()) return false;
    const int e = SynthKey::engine(key);
    return e >= 0 && J::stereo(e);
}

double pget(const ParamMap& m, const char* id) {
    const auto it = m.find(id);
    return it == m.end() ? 0 : it->second;
}

bool assignPan(const std::map<std::string, ModAssign>& a) {
    const auto it = a.find("pan");
    return it != a.end() && std::abs(it->second.amount) > 0;
}

bool clipStereo(const Clip& c) {
    if (c.src) {
        if (soundStereo(c.src->sample)) return true;
        if (std::abs(pget(c.src->params, "pan")) >= 1 || assignPan(c.src->assigns)) return true;
    }
    for (const auto& page : c.pages)
        for (const auto& s : page) {
            if (std::abs(pget(s.locks, "pan")) >= 1) return true;
            if (s.sound && soundStereo(*s.sound)) return true;
        }
    return false;
}

bool panLane(const ArrAutoLane& l) {
    if (l.param != "pan") return false;
    for (const auto& p : l.points)
        if (std::abs(p.y * 127 - 64) >= 1) return true;
    return false;
}
}  // namespace

bool App::trackStereo(int t) const {
    if (t < 0 || t >= int(project_.tracks.size())) return false;
    const Track& tr = project_.tracks[size_t(t)];
    if (soundStereo(tr.sample)) return true;
    if (std::abs(pget(tr.params, "pan")) >= 1 || assignPan(tr.assigns)) return true;
    if (tr.home) {
        if (soundStereo(tr.home->sample)) return true;
        if (std::abs(pget(tr.home->params, "pan")) >= 1 || assignPan(tr.home->assigns)) return true;
    }
    for (const auto& c : tr.clips)
        if (c && clipStereo(*c)) return true;
    for (const auto& b : tr.blocks) {
        if (b.pattern && clipStereo(*b.pattern)) return true;
        for (const auto& l : b.autos)
            if (panLane(l)) return true;
    }
    for (const auto& l : tr.autos)
        if (panLane(l)) return true;
    return false;
}

void App::setLevelRelative(bool on) {
    levelRelative_ = on;
    publishAudio();
    bump();
}

}  // namespace jambo
