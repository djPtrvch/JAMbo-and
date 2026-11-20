// App: шаги, ручки с p-lock, клавиатура (порт AppModelEdits.swift, AppModelPiano.swift, части AppModelSound.swift,
// ветка swift JAMbo). Без MORPH, FX/MOD, LFO/ENV, кривых аранжировки, CHORD и записи с REC — позже.
#include "App.h"
#include "model/FilterType.h"
#include "model/Scales.h"
#include <algorithm>

namespace jambo {

namespace {
/// Клавиатура: одна октава гаммы + верхняя корневая (Settings.pianoOctaves по умолчанию; меню клавиатуры — позже).
constexpr int pianoOctaves = 1;
}  // namespace

// MARK: — шаги

std::pair<int, int> App::viewPages() const {
    const Track& t = seq();
    const int n = int(t.pages().size());
    const int v = std::max(0, std::min(viewPage_, n - 1));
    if (t.playTo() > t.playFrom() && t.playTo() < n && t.playFrom() <= v && v <= t.playTo())
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
        const ParamDef& p = Params::grid(track().sample)[size_t(slot)];
        if (p.id.empty()) return nullptr;
        if (p.id == "filter") {
            const auto& pm = track().params;
            const auto it = pm.find(Params::filterTypeId);
            const int type = FilterType::effective(it == pm.end() ? FilterType::of(nullptr) : FilterType::of(it->second),
                                                   track().sample);
            return &Params::filterDef(type);
        }
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
        const auto& pm = track().params;
        const auto it = pm.find(p->id);
        if (it != pm.end()) base = it->second;
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
    const auto& pm = track().params;
    const auto it = pm.find(p->id);
    return p->toNorm(it == pm.end() ? p->def : it->second);
}

void App::setKnob(int slot, double norm) {
    const ParamDef* p = param(slot);
    if (!p) return;
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
    if (held_.empty()) {
        const int i = selected_;
        edit([&](Project& pr) { pr.tracks[size_t(i)].params[id] = v; });
    } else {
        for (int i : held_) used_.insert(i);
        const auto held = held_;
        editTrack([&](Track& t) { for (int i : held) t.setLock(i, id, v); });
    }
}

/// Двойной тап: с зажатым шагом — снять p-lock; без — значение по умолчанию (свои значения по умолчанию — позже).
void App::knobDoubleTap(int slot) {
    const ParamDef* p = param(slot);
    if (!p) return;
    const std::string id = p->id;
    const double def = p->def;
    if (channel_ == Channel::mix) {
        if (held_.empty()) return;  // меню дорожки — позже
        for (int i : held_) used_.insert(i);
        const auto held = held_;
        editSeq([&](Track& t) { for (int i : held) t.clearLock(i, id); });
        return;
    }
    if (channel_ == Channel::master) {
        edit([&](Project& pr) { pr.master[id] = def; });
        return;
    }
    if (held_.empty()) {
        const int i = selected_;
        edit([&](Project& pr) { pr.tracks[size_t(i)].params[id] = def; });
        return;
    }
    for (int i : held_) used_.insert(i);
    const auto held = held_;
    editTrack([&](Track& t) { for (int i : held) t.clearLock(i, id); });
}

// MARK: — клавиатура

int App::octave() const {
    return std::min(track().keyOctave, Scales::topOctave(project_.scale, project_.root, pianoOctaves, false));
}

int App::keyCount() const { return Scales::keyCount(project_.scale, pianoOctaves); }

int App::keyNote(int k) const { return Scales::note(project_.scale, project_.root, octave(), k); }

std::optional<std::vector<int>> App::keyNotes(int k) const {
    if (k < 0 || k >= keyCount()) return std::nullopt;
    return std::vector<int>{keyNote(k)};
}

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
        audio_->push(CoreEvent::noteOn(t, *it, id, float(velocity)));
}

void App::keyDown(int id, int k, double velocity) {
    auto notes = keyNotes(k);
    if (!notes) return;
    const int ti = selected_;
    pressedKeys_[id] = *notes;
    // Velocity клавиш (VELOCITY в меню клавиатуры) — позже: ноты звучат с Level дорожки.
    (void)velocity;
    liveNoteOn(ti, *notes, id, 1);
    keyHolds_[id] = {ti, *notes};
    inputNotes();
    bump();
}

void App::keyUp(int id) {
    pressedKeys_.erase(id);
    const auto it = keyHolds_.find(id);
    if (it != keyHolds_.end()) {
        audio_->push(CoreEvent::noteOff(it->second.track, id));
        keyHolds_.erase(it);
    }
    bump();
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

void App::setOctave(int o) {
    const int top = Scales::topOctave(project_.scale, project_.root, pianoOctaves, false);
    const int o2 = std::min(top, std::max(Scales::octaveMin, o));
    if (o2 == octave()) return;
    const int i = selected_;
    mutatePlay([&](Project& p) { p.tracks[size_t(i)].keyOctave = o2; }, true);
}

void App::midiNote(int note, int velocity) {
    if (note < 0 || note > 127) return;
    // id живой ноты MIDI — вне id касаний экрана.
    const int id = 1'000'000 + note;
    if (velocity > 0) {
        liveNoteOn(selected_, {note}, id, double(velocity) / 127.0);
        keyHolds_[id] = {selected_, {note}};
    } else {
        const auto it = keyHolds_.find(id);
        if (it == keyHolds_.end()) return;
        audio_->push(CoreEvent::noteOff(it->second.track, id));
        keyHolds_.erase(it);
    }
}

}  // namespace jambo
