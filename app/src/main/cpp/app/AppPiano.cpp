// App: гамма, корневая нота, октава, клавиши, режим CHORD и типы аккорда, меню клавиатуры (1 / 2 OCTAVES, SWING,
// VELOCITY; порт частей AppModelPiano.swift и AppModelChords.swift, ветка swift JAMbo). ARP, RANDOM (SHIFT + CHORD,
// стили ударных) и раздел PART — позже.
#include "App.h"
#include "model/Chords.h"
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

bool App::chordOn() const { return chordMode_[size_t(selected_)]; }

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
