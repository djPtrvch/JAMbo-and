// App: меню звука, стрелки папки дорожки, данные окна звука (порт части AppModelSound.swift, SoundMenu.swift и
// SynthWindow.swift, ветка swift JAMbo).
#include "App.h"
#include "model/AnalogMod.h"
#include "model/SynthEngines.h"
#include "synth/Jambo.h"
#include "synth/Wavetable.h"
#include <algorithm>

namespace jambo {

std::optional<int> App::firstHeld() const {
    if (channel_ != Channel::track || held_.empty()) return std::nullopt;
    return *held_.begin();
}

std::string App::shownSound() const {
    if (auto h = firstHeld())
        if (const Step* s = track().step(*h))
            if (s->sound) return *s->sound;
    return track().sample;
}

double App::synthValue(const ParamDef& p) const {
    const Track& t = track();
    const auto lockAt = [&](int i) -> std::optional<double> {
        const Step* s = t.step(i);
        if (!s) return std::nullopt;
        const auto it = s->locks.find(p.id);
        if (it == s->locks.end()) return std::nullopt;
        return it->second;
    };
    if (auto h = firstHeld()) {
        if (auto v = lockAt(*h)) return *v;
    } else if (playing_) {
        const int pos = rawTrackPlayPos();
        if (pos >= 0)
            if (auto v = lockAt(pos)) return *v;
    }
    const auto it = t.params.find(p.id);
    return it == t.params.end() ? p.def : it->second;
}

std::string App::sampleName(const std::string& key) const {
    if (key.empty()) return "No sample";
    const auto it = project_.files.find(key);
    return it == project_.files.end() ? "file" : it->second;
}

SoundInfo App::soundInfo() const {
    SoundInfo s;
    if (channel_ != Channel::track) return s;
    const Track& t = track();
    const auto pos = t.folderPos(firstHeld());
    s.folderTotal = int(t.folder.items.size());
    s.folderLabel = pos.second == 0 ? "folder"
                                    : (pos.first > 0 ? std::to_string(pos.first) : "–") + " / " +
                                          std::to_string(pos.second);
    const std::string key = shownSound();
    const int e = SynthKey::engine(key);
    s.engine = e;
    if (e < 0) {
        s.title = key.empty() ? "No sample" : sampleName(key);
        if (!key.empty()) {
            const auto it = project_.roots.find(key);
            s.rows.push_back("Note · " + (it == project_.roots.end() ? std::string("—")
                                                                            : Params::noteName(it->second)));
        }
        return s;
    }
    const SynthEngine* se = SynthEngines::engine(e);
    s.title = se ? se->name : "";
    // У WTABLE после имени движка — имя таблицы дорожки (своя таблица — позже, с библиотекой), у GRANULAR — сэмпла.
    if (e == J::wtable) s.detail = SynthEngines::capitalized(WavetableKey::name(t.table));
    if (e == J::granular) s.detail = sampleName(t.grain);
    if (SynthKey::engine(t.sample) == e) s.preset = t.presetName();
    const auto& grid = Params::synthGrids()[size_t(e)];
    for (int slot = 0; slot < 4; ++slot) {
        const ParamDef& p = AnalogMod::knob(grid[size_t(slot)], e, t.params);
        const auto it = t.params.find(p.id);
        std::string v = p.format ? p.format(it == t.params.end() ? p.def : it->second) : "";
        std::replace(v.begin(), v.end(), '\n', ' ');
        s.rows.push_back(p.label + " · " + v);
    }
    return s;
}

// MARK: — меню звука

void App::openSoundMenu() {
    if (channel_ != Channel::track) return;
    std::vector<int> steps(held_.begin(), held_.end());
    for (int i : steps) used_.insert(i);
    soundMenu_ = steps;
    bump();
}

void App::closeSoundMenu() {
    if (!soundMenu_) return;
    soundMenu_.reset();
    bump();
}

void App::pickSynth(int engine) {
    if (!soundMenu_) return;
    const auto steps = *soundMenu_;
    closeSoundMenu();
    if (steps.empty()) {
        const KnobDefaults* kd = &knobDefaults_;
        editTrack([&](Track& t) { t.setSynth(engine, kd); });
    } else {
        const std::string key = SynthKey::key(engine);
        editTrack([&](Track& t) { t.setStepSound(steps, key); });
    }
}

void App::cleanSampler() {
    if (!soundMenu_ || !soundMenu_->empty()) return;
    closeSoundMenu();
    const KnobDefaults* kd = &knobDefaults_;
    editTrack([&](Track& t) {
        // С синта на сэмплер — ручки сэмплера по своим значениям по умолчанию.
        const bool fromSynth = SynthKey::engine(t.sample) >= 0;
        t.sample = "";
        if (fromSynth) kd->applySound(t);
    });
    // Редактор сэмпла — позже (на Android пока нет).
}

void App::stepSoundToTrack() {
    if (!soundMenu_ || soundMenu_->empty()) return;
    const auto steps = *soundMenu_;
    closeSoundMenu();
    editTrack([&](Track& t) { t.setStepSound(steps, std::nullopt); });
}

void App::folderArrow(int dir) {
    if (channel_ != Channel::track) return;
    if (held_.empty()) {
        editTrack([&](Track& t) { t.stepFolder(dir); });
    } else {
        for (int i : held_) used_.insert(i);
        const std::vector<int> held(held_.begin(), held_.end());
        editTrack([&](Track& t) { t.stepFolderOnSteps(held, dir); });
    }
}

}  // namespace jambo
