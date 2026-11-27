// App: гамма, корневая нота, октава, клавиши, режим CHORD и типы аккорда, меню клавиатуры (1 / 2 OCTAVES, SWING,
// VELOCITY; порт частей AppModelPiano.swift, AppModelArp.swift и AppModelChords.swift, ветка swift JAMbo).
#include "App.h"
#include "model/Chords.h"
#include "model/DrumRhythm.h"
#include "model/Scales.h"
#include <algorithm>

namespace jambo {

void App::setScale(int v) {
    const int n = Scales::wrap(v, int(Scales::all.size()));
    if (n == project_.scale) return;
    edit([&](Project& p) {
        p.scale = n;
        writeLead(p);
    });
}

void App::setRoot(int v) {
    const int n = Scales::wrap(v, 12);
    if (n == project_.root) return;
    edit([&](Project& p) {
        p.root = n;
        writeLead(p);
    });
}

void App::setPianoSettings(int octaves, bool velocity) {
    pianoOctaves_ = std::clamp(octaves, 1, 2);
    velocity_ = velocity;
    bump();
}

bool App::chordOn() const { return !arp().mode && !randomButton() && chordMode_[size_t(selected_)]; }

const TrackArp& App::arp() const {
    static const TrackArp off;
    return channel_ == Channel::track ? track().arp : off;
}

void App::syncArp() {
    for (size_t i = 0; i < sentArp_.size(); ++i) {
        TrackArp a = i < project_.tracks.size() ? project_.tracks[i].arp : TrackArp{};
        a.on = a.active();
        if (a == sentArp_[i]) continue;
        sentArp_[i] = a;
        audio_->push(CoreEvent::arp(int(i), a.on, a.dir, a.rate, a.octaves, float(a.gate), a.latch));
    }
}

void App::editArp(const std::function<void(TrackArp&)>& change) {
    if (channel_ != Channel::track) return;
    TrackArp next = arp();
    change(next);
    if (next == arp()) return;
    mutatePlay([&](Project& p) { p.tracks[size_t(selected_)].arp = next; }, false);
    syncArp();
}

void App::setArpMode(bool on) {
    if (channel_ != Channel::track || on == arp().mode) return;
    const bool enabled = on ? chordMode_[size_t(selected_)] : arp().on;
    chordMode_[size_t(selected_)] = !on && enabled;
    editArp([&](TrackArp& a) { a.mode = on; a.on = on && enabled; });
}

void App::setArpRate(int rate) {
    editArp([&](TrackArp& a) { a.rate = Arp::clampRate(rate); });
}

void App::setArpOctaves(int octaves) {
    editArp([&](TrackArp& a) { a.octaves = Arp::clampOctaves(octaves); });
}

void App::setArpGate(double gate) {
    if (std::isfinite(gate)) editArp([&](TrackArp& a) { a.gate = Arp::clampGate(std::round(gate * 100) / 100); });
}

void App::setArpLatch(bool on) {
    editArp([&](TrackArp& a) { a.latch = on; });
}

void App::chordTap() {
    if (shiftHeld_ || randomButton()) {
        if (channel_ == Channel::track) writeRandom();
        return;
    }
    if (arp().mode) editArp([](TrackArp& a) { a.on = !a.on; });
    else toggleChord();
}

void App::chooseChordType(int k) {
    if (arp().mode) editArp([&](TrackArp& a) { a.dir = Arp::clampDir(k); });
    else if (chordStyle() >= 0) {
        const int style = DrumRhythm::styleIndex(k);
        edit([&](Project& p) { for (auto& t : p.tracks) t.drumStyle = style; });
    } else setChordKind(k);
}

int App::chordKind() const { return chordKind_[size_t(selected_)]; }

void App::toggleChord() {
    chordMode_[size_t(selected_)] = !chordMode_[size_t(selected_)];
    bump();
}

void App::setChordKind(int k) {
    chordKind_[size_t(selected_)] = std::min(int(Scales::chordKinds.size()) - 1, std::max(0, k));
    bump();
}

std::string App::chordKeyLabel(int k) const {
    const std::string label = Chords::keyLabel(project_.scale, project_.root, octave(), k, chordKind());
    return label + "\n" + Params::noteName(keyNote(k));
}

int App::topOctave() const {
    return Scales::topOctave(project_.scale, project_.root, pianoOctaves_, chordOn(), chordKind());
}

int App::octave() const { return std::min(track().keyOctave, topOctave()); }

int App::keyCount() const { return Scales::keyCount(project_.scale, pianoOctaves_); }

int App::keyNote(int k) const { return Scales::note(project_.scale, project_.root, octave(), k); }

std::optional<std::vector<int>> App::keyNotes(int k) const {
    if (k < 0 || k >= keyCount()) return std::nullopt;
    if (chordOn()) return Scales::chord(project_.scale, project_.root, octave(), k, chordKind());
    return std::vector<int>{keyNote(k)};
}

void App::setOctave(int o) {
    const int o2 = std::min(topOctave(), std::max(Scales::octaveMin, o));
    if (o2 == octave()) return;
    const int i = selected_;
    // Хранится в проекте у дорожки, без записи UNDO.
    mutatePlay([&](Project& p) { p.tracks[size_t(i)].keyOctave = o2; }, true);
}

double App::trackSwing() const {
    const auto& ps = track().params;
    const auto it = ps.find("swing");
    return it == ps.end() ? Params::swing().def : it->second;
}

void App::setSwing(double v) {
    const ParamDef& p = Params::swing();
    const double x = std::min(p.max, std::max(p.min, v));
    editTrack([&](Track& t) { t.params["swing"] = x; });
}

}  // namespace jambo
