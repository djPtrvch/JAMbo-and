// App: шаги, ручки с p-lock, клавиатура (порт AppModelEdits.swift, AppModelPiano.swift, части AppModelSound.swift,
// ветка swift JAMbo). Гамма, октава и CHORD — AppPiano.cpp.
#include "App.h"
#include "model/AnalogMod.h"
#include "model/FilterType.h"
#include "model/Recording.h"
#include "model/SynthEngines.h"
#include "model/Scales.h"
#include <algorithm>
#include <chrono>

namespace jambo {

namespace {
double steadyNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

// MARK: — шаги

std::pair<int, int> App::viewPages() const {
    const Track& t = seq();
    const int n = int(t.pages().size());
    const int v = std::max(0, std::min(viewPage_, n - 1));
    if (spanView_ && t.playTo() > t.playFrom() && t.playTo() < n && t.playFrom() <= v && v <= t.playTo())
        return {t.playFrom(), t.playTo()};
    return {v, v};
}

StepInfo App::stepInfo(int page, int col) const {
    StepInfo i;
    const Track& t = seq();
    const int index = page * Params::stepsPerPage + col;
    const Step* s = t.step(index);
    const bool chan = channel_ == Channel::track;
    i.on = chan && s && s->on;
    if (chan && !i.on) {
        for (int k = index - 1; k >= 0 && k >= index - 128; --k) {
            if (const Step* p = t.step(k); p && p->on) {
                i.tail = p->len > index - k;
                break;
            }
        }
    }
    if (s) {
        const auto kinds = Track::manualLockKinds(*s);
        i.manualLock = kinds.other;
        i.col4Lock = kinds.col4;
    }
    i.ownSound = i.on && s->sound.has_value();
    if (i.on) i.label = s->notes.size() > 1 ? "POLY" : (s->notes.empty() ? "" : Params::noteName(s->notes[0]));
    i.playing = openPlayPos() == index;
    i.held = held_.count(index) != 0;
    const int len = t.pageLen(page);
    i.outside = col >= len;
    i.last = len < Params::stepsPerPage && col == len - 1;
    return i;
}

void App::holdStep(int index, bool on) {
    if (on) {
        held_.insert(index);
    } else {
        held_.erase(index);
        used_.erase(index);
    }
    bump();
}

std::vector<int> App::trigNotes() const {
    std::vector<int> sel = selNotes_[size_t(selected_)];
    if (sel.empty()) return {Scales::baseNote(project_.root, octave())};
    std::sort(sel.begin(), sel.end());
    return sel;
}

void App::toggleStep(int index) {
    if (channel_ != Channel::track) return;
    const auto notes = trigNotes();
    editTrack([&](Track& t) { t.toggleStep(index, notes); });
}

bool App::paintDrumSteps(int first, int last) {
    if (channel_ != Channel::track || first < 0 || last < first || last >= Params::maxPages * Params::stepsPerPage) return false;
    const auto& sound = track().sample;
    if (track().drumRole() < 0 && (sound.empty() || SynthKey::engine(sound) >= 0)) return false;
    const auto notes = trigNotes();
    editTrack([&](Track& t) {
        for (int i = first; i <= last; ++i) {
            t.withStep(i, [&](Step& s) {
                if (!s.on) {
                    s.on = true;
                    if (s.notes.empty()) s.notes = notes;
                }
            });
        }
    });
    return true;
}

void App::setNoteLength(int index, int len) {
    if (channel_ != Channel::track) return;
    const auto notes = trigNotes();
    editTrack([&](Track& t) {
        t.withStep(index, [&](Step& s) {
            if (!s.on) {
                s.on = true;
                if (s.notes.empty()) s.notes = notes;
            }
            s.setAllLen(len);
        });
    });
}

bool App::copyStep(int a, int b) {
    const Step* src = seq().step(a);
    const Step* old = seq().step(b);
    if (a == b || !src || !old) return false;
    const Step from = *src;
    lastCopy_ = std::make_pair(b, *old);
    editSeq([&](Track& t) { t.withStep(b, [&](Step& s) { s = from; }); });
    return true;
}

void App::restoreCopy(int index) {
    if (!lastCopy_ || lastCopy_->first != index) return;
    const Step old = lastCopy_->second;
    lastCopy_.reset();
    editSeq([&](Track& t) { t.withStep(index, [&](Step& s) { s = old; }); });
}

void App::setPatternLength(int lastIndex) {
    for (int i : held_) used_.insert(i);
    editSeq([&](Track& t) { t.setLength(lastIndex + 1); });
}

// MARK: — ручки

const ParamDef* App::param(int slot) const {
    if (slot < 0 || slot >= 16) return nullptr;
    switch (channel_) {
    case Channel::track: {
        if (modMode()) return &modParam(slot);
        if (fxMode()) return &fxParam(slot);
        const ParamDef& p = Params::grid(track().sample)[size_t(slot)];
        if (p.id == "rate" && rateStretch()) return &Params::rateTstr();
        if (p.id == "filter") return &Params::filterDef(filterType());
        if (p.id.empty()) return nullptr;
        // ANALOG: AMOUNT вместо DETUNE при модуляции, WAVE B с «−1» при октаве ниже.
        const int e = SynthKey::engine(track().sample);
        if (e >= 0) return &AnalogMod::knob(p, e, track().params);
        return &p;
    }
    case Channel::mix:
        if (slot >= int(project_.tracks.size())) return nullptr;
        return &Params::mixGrid()[size_t(slot)];
    case Channel::master: return &Params::masterGrid()[size_t(slot)];
    }
    return nullptr;
}

std::optional<double> App::playLock(const std::string& id) const {
    const int pos = openPlayPos();
    if (pos < 0) return std::nullopt;
    const Step* s = seq().step(pos);
    if (!s) return std::nullopt;
    const auto it = s->locks.find(id);
    if (it == s->locks.end()) return std::nullopt;
    return it->second;
}

KnobState App::knobState(int slot) const {
    KnobState st;
    const ParamDef* p = param(slot);
    if (!p) return st;
    st.param = p;
    st.label = channel_ == Channel::mix ? project_.tracks[size_t(slot)].name : p->label;
    const auto lockOf = [&](const Track& t) -> std::optional<double> {
        if (!held_.empty()) {
            const Step* s = t.step(*held_.begin());
            if (!s) return std::nullopt;
            const auto it = s->locks.find(p->id);
            if (it == s->locks.end()) return std::nullopt;
            return it->second;
        }
        if (playing_) return playLock(p->id);
        return std::nullopt;
    };
    double base = p->def;
    std::optional<double> lock;
    if (channel_ == Channel::mix) {
        base = project_.tracks[size_t(slot)].vol;
        lock = lockOf(seq());
    } else if (channel_ == Channel::master) {
        const auto it = project_.master.find(p->id);
        base = it == project_.master.end() ? p->def : it->second;
    } else {
        base = baseValue(*p);
        lock = lockOf(track());
        st.col4 = slot / 4 == 3;
    }
    st.value = lock.value_or(base);
    st.norm = p->toNorm(st.value);
    st.baseNorm = p->toNorm(base);
    st.locked = lock.has_value();
    if (channel_ == Channel::track) {
        st.assign = assign(slot);
        if (st.assign) {
            const auto r = sourceRange(*st.assign);
            st.srcLo = r.first;
            st.srcHi = r.second;
            st.heldMatch = heldSource() == st.assign->lfo;
        }
    }
    return st;
}

double App::knobStart(int slot) const {
    const ParamDef* p = param(slot);
    if (!p) return 0;
    const auto heldLock = [&](const Track& t) -> std::optional<double> {
        if (held_.empty()) return std::nullopt;
        const Step* s = t.step(*held_.begin());
        if (!s) return std::nullopt;
        const auto it = s->locks.find(p->id);
        if (it == s->locks.end()) return std::nullopt;
        return it->second;
    };
    if (channel_ == Channel::mix) {
        if (auto v = heldLock(seq())) return p->toNorm(*v);
        return p->toNorm(project_.tracks[size_t(slot)].vol);
    }
    if (channel_ == Channel::master) {
        const auto it = project_.master.find(p->id);
        return p->toNorm(it == project_.master.end() ? p->def : it->second);
    }
    if (auto v = heldLock(track())) return p->toNorm(*v);
    if (arrangePlaying_ && !fxMode() && !modMode()) {
        const int target = ArrAutoTarget::index(p->id);
        if (target >= 0 && !arrangeAutoHeld_.count(selected_ * ArrAutoTarget::count + target)) {
            if (auto value = track().autoValue(p->id, arrangePositionX())) return *value;
        }
    }
    return p->toNorm(baseValue(*p));
}

void App::setKnob(int slot, double norm) {
    const ParamDef* p = param(slot);
    if (!p) return;
    if (recordArrangeAutoPoint(slot, *p, norm)) return;
    const double v = p->fromNorm(norm);
    const std::string id = p->id;
    if (channel_ == Channel::mix) {
        if (held_.empty()) {
            edit([&](Project& pr) { pr.tracks[size_t(slot)].vol = v; });
        } else {
            for (int i : held_) used_.insert(i);
            const auto held = held_;
            editSeq([&](Track& t) { for (int i : held) t.setLock(i, id, v); });
        }
        return;
    }
    if (channel_ == Channel::master) {
        edit([&](Project& pr) { pr.master[id] = v; });
        return;
    }
    if (held_.empty() && fxMode()) {
        // MORPH: Reverb, Delay, Drive — как ручки дорожки (исходное, точка, RETURN; без UNDO); SHIFT + MORPH — у всех.
        if (morph_ && Project::trackFx(id)) {
            const bool all = morphAllNow();
            const int i = selected_;
            const ParamDef def = *p;
            editMorph([&](Project& pr) {
                pr.morphFx(i, id);
                pr.setFx(i, id, v);
                if (all) pr.morphShiftFx(i, def, norm);
            });
        } else {
            setFx(*p, v);
        }
    } else if (held_.empty()) {
        const int i = selected_;
        if (morph_) {
            // MORPH — без UNDO; на все дорожки — сдвиг той же ручки на ту же долю хода.
            const bool all = morphAllNow();
            const ParamDef def = *p;
            editMorph([&](Project& pr) {
                pr.tracks[size_t(i)].morphParam(id);
                pr.tracks[size_t(i)].params[id] = v;
                if (all) pr.morphShift(i, def, norm);
            });
        } else {
            edit([&](Project& pr) { pr.tracks[size_t(i)].params[id] = v; });
        }
    } else {
        for (int i : held_) used_.insert(i);
        const auto held = held_;
        editTrack([&](Track& t) { for (int i : held) t.setLock(i, id, v); });
    }
}

/// Двойной тап: с зажатым шагом — снять p-lock; с SHIFT — снять все p-lock дорожки; без — значение по умолчанию
/// (свои значения по умолчанию — позже).
void App::knobDoubleTap(int slot) {
    if (channel_ == Channel::mix) {
        const ParamDef* p = param(slot);
        // Без зажатых шагов — меню дорожки (у серой ручки — ADD TRACK).
        if (held_.empty()) {
            openTrackMenu(slot);
            return;
        }
        if (!p) return;
        const std::string id = p->id;
        for (int i : held_) used_.insert(i);
        const auto held = held_;
        editSeq([&](Track& t) { for (int i : held) t.clearLock(i, id); });
        return;
    }
    if (channel_ == Channel::master && !knobShift()) {
        resetKnob(slot);
        return;
    }
    const ParamDef* p = param(slot);
    if (!p || channel_ != Channel::track) return;
    if (knobShift()) {
        editTrack([](Track& t) { t.clearAllLocks(); });
        return;
    }
    if (held_.empty()) {
        resetKnob(slot);
        return;
    }
    const std::string id = p->id;
    for (int i : held_) used_.insert(i);
    const auto held = held_;
    editTrack([&](Track& t) { for (int i : held) t.clearLock(i, id); });
}

// MARK: — клавиатура

std::set<int> App::litNotes() const {
    std::set<int> all;
    for (const auto& [id, n] : pressedKeys_) all.insert(n.begin(), n.end());
    if (channel_ == Channel::track && !held_.empty())
        if (const Step* s = track().step(*held_.begin()); s && s->on) all.insert(s->notes.begin(), s->notes.end());
    if (playing_ && (flashMask_ >> selected_ & 1u)) {
        const int pos = rawTrackPlayPos();
        if (pos >= 0)
            if (const Step* s = track().step(pos)) all.insert(s->notes.begin(), s->notes.end());
    }
    return all;
}

std::set<int> App::shownNotes() const {
    if (channel_ == Channel::track && !held_.empty()) {
        const Step* s = track().step(*held_.begin());
        return s ? std::set<int>(s->notes.begin(), s->notes.end()) : std::set<int>{};
    }
    const auto& sel = selNotes_[size_t(selected_)];
    return sel.empty() ? std::set<int>{keyNote(0)} : std::set<int>(sel.begin(), sel.end());
}

void App::liveNoteOn(int t, const std::vector<int>& notes, int id, double velocity) {
    // Сверху вниз: корень аккорда — последним (монофонический движок оставляет последнюю ноту).
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
        audio_->push(CoreEvent::noteOn(t, *it, id, velocity_ ? float(velocity) : 1.0f));
}

void App::keyDown(int id, int k, double velocity, bool screenKey) {
    auto notes = keyNotes(k);
    if (!notes) return;
    const int ti = selected_;
    pressedKeys_[id] = *notes;
    liveNoteOn(ti, *notes, id, velocity);
    std::optional<int> recIndex;
    if (recOn_ && playing_ && held_.empty() && channel_ == Channel::track) {
        if (!arp().active()) recIndex = record(ti, *notes, velocity_ ? std::optional<double>(velocity) : std::nullopt);
    } else {
        inputNotes();
    }
    std::optional<int> arrangeNote;
    if (arrangeRecorder_.active() && held_.empty() && channel_ == Channel::track) {
        if (const auto tick = heardTick()) {
            const int step = arrangeLive_ + (*tick - std::max(0, arrangeTick_));
            arrangeNote = arrangeRecorder_.liveNote(ti, step, *notes,
                velocity_ ? std::optional<double>(velocity) : std::nullopt);
        }
    }
    keyHolds_[id] = {ti, *notes, recIndex, steadyNow(), screenKey, arrangeNote};
    bump();
}

void App::keyUp(int id) {
    pressedKeys_.erase(id);
    const auto it = keyHolds_.find(id);
    if (it != keyHolds_.end()) {
        const KeyHold k = it->second;
        audio_->push(CoreEvent::noteOff(k.track, id));
        keyHolds_.erase(it);
        // С REC — длина записанной ноты по времени удержания.
        if (k.recIndex || k.arrangeNote) {
            const double stepSeconds = 60 / project_.bpm / 4;
            const int len = RecordClock::length(steadyNow() - k.t0, stepSeconds);
            if (k.arrangeNote) arrangeRecorder_.liveLength(*k.arrangeNote, len);
            if (k.recIndex) {
                const int index = *k.recIndex;
                writeTake(k.track, [&](Track& t) { t.recordLength(index, len); });
            }
        }
    }
    bump();
}

// Android может прервать касание клавиатуры без его обычного отпускания.
void App::releaseScreenKeys() {
    for (auto it = keyHolds_.begin(); it != keyHolds_.end();) {
        const auto key = it++;
        if (key->second.screenKey) keyUp(key->first);
    }
}

/// Нажатые ноты: зажатые шаги получают их, иначе они — выбранные ноты дорожки.
void App::inputNotes() {
    std::vector<int> notes;
    for (int n : litNotesPressed()) notes.push_back(n);
    if (notes.empty()) return;
    if (!held_.empty() && channel_ == Channel::track) {
        for (int i : held_) used_.insert(i);
        const auto held = held_;
        editTrack([&](Track& t) {
            for (int i : held)
                t.withStep(i, [&](Step& s) {
                    s.setNotes(notes);
                    s.on = true;
                });
        });
        return;
    }
    selNotes_[size_t(selected_)] = notes;
}

std::set<int> App::litNotesPressed() const {
    std::set<int> all;
    for (const auto& [id, n] : pressedKeys_) all.insert(n.begin(), n.end());
    return all;
}

void App::midiNote(int note, int velocity) {
    midiNoteFrom(note, velocity, 1'000'000 + note);
}

void App::midiNoteFrom(int note, int velocity, int id) {
    if (note < 0 || note > 127) return;
    if (velocity > 0) {
        if (const auto it = keyHolds_.find(id); it != keyHolds_.end())
            audio_->push(CoreEvent::noteOff(it->second.track, id));
        liveNoteOn(selected_, {note}, id, double(velocity) / 127.0);
        keyHolds_[id] = {selected_, {note}, std::nullopt};
    } else {
        const auto it = keyHolds_.find(id);
        if (it == keyHolds_.end()) return;
        audio_->push(CoreEvent::noteOff(it->second.track, id));
        keyHolds_.erase(it);
    }
}

}  // namespace jambo
