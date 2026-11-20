// App: транспорт, правки и UNDO, выбор дорожки, ход игры (порт AppModel.swift и LiveState.swift, ветка swift JAMbo).
#include "App.h"
#include "sequencer/SeqData.h"
#include <algorithm>

namespace jambo {

App::App(AppAudio* audio)
    : audio_(audio), playPos_(size_t(Params::maxTracks + SeqData::chans), -1), cell_(size_t(Params::maxTracks), -1) {
    publishAudio();
}

void App::loadProject(const Project& p) {
    if (playing_) {
        playing_ = false;
        audio_->push(CoreEvent::play(false));
    }
    history_.reset();
    endRecTake();
    selected_ = 0;
    channel_ = Channel::track;
    viewPage_ = 0;
    held_.clear();
    used_.clear();
    project_ = p;
    audio_->push(CoreEvent::tempo(p.bpm));
    publishAudio();
    bump();
}

// MARK: — транспорт

void App::togglePlay() {
    playing_ = !playing_;
    endRecTake();
    if (!playing_) trackStart_.fill(0);
    if (playing_) {
        audio_->push(CoreEvent::arrange(false, 0));
    } else {
        beatFlash_ = false;
    }
    audio_->push(CoreEvent::play(playing_));
    bump();
}

void App::toggleMetronome() {
    metronome_ = !metronome_;
    audio_->push(CoreEvent::metronome(metronome_));
    bump();
}

void App::setBpm(double value) {
    const double bpm = std::min(Params::bpmMax, std::max(Params::bpmMin, std::round(value * 10) / 10));
    if (bpm == project_.bpm) return;
    edit([&](Project& p) { p.bpm = bpm; });
    audio_->push(CoreEvent::tempo(bpm));
}

// MARK: — правки и UNDO

void App::edit(const std::function<void(Project&)>& change) {
    const Project before = project_;
    change(project_);
    // Новые блоки аранжировки — со своей копией клипа (AppModel.edit).
    if (needsOwnPatterns(project_)) project_.ownArrPatterns();
    if (project_ == before) return;
    history_.record(before, project_);
    changed();
}

bool App::needsOwnPatterns(const Project& p) {
    for (const auto& t : p.tracks)
        for (const auto& b : t.blocks)
            if (!b.pattern) return true;
    return false;
}

void App::editTrack(const std::function<void(Track&)>& change) {
    const int i = selected_;
    edit([&](Project& p) { change(p.tracks[size_t(i)]); });
}

void App::editSeq(const std::function<void(Track&)>& change) {
    switch (channel_) {
    case Channel::track: editTrack(change); break;
    case Channel::mix: edit([&](Project& p) { change(p.chanSeqs[Project::mixSeq]); }); break;
    case Channel::master: edit([&](Project& p) { change(p.chanSeqs[Project::mstSeq]); }); break;
    }
}

void App::mutatePlay(const std::function<void(Project&)>& change, bool publish) {
    change(project_);
    if (publish) publishAudio();
    projectChanged();
    bump();
}

void App::beginGesture() { history_.beginGesture(project_); }

void App::endGesture() {
    history_.endGesture(project_);
    bump();
}

void App::undo() {
    if (auto prev = history_.undo(project_)) restore(*prev);
}

void App::redo() {
    if (auto next = history_.redo(project_)) restore(*next);
}

/// Вернуть проект к снимку: что играет, mute, solo, октава клавиатуры — UNDO не меняет.
void App::restore(const Project& snap) {
    Project p = snap;
    for (auto& t : p.tracks) {
        const auto cur = std::find_if(project_.tracks.begin(), project_.tracks.end(),
                                      [&](const Track& c) { return c.id == t.id; });
        if (cur == project_.tracks.end()) continue;
        t.stopped = cur->stopped;
        t.mute = cur->mute;
        t.solo = cur->solo;
        t.arp = cur->arp;
        t.keyOctave = cur->keyOctave;
        if (t.clipFilled(cur->slot) || cur->blank()) t.moveSlot(cur->slot);
        t.open = cur->open;
        t.settleSound();
    }
    // MORPH — без UNDO: накрученное в режиме остаётся, снимок меняет только то, к чему MORPH вернётся.
    p.keepMorph(project_);
    const bool bpmChanged = p.bpm != project_.bpm;
    project_ = std::move(p);
    held_.clear();
    selected_ = std::min(selected_, int(project_.tracks.size()) - 1);
    viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
    if (bpmChanged) audio_->push(CoreEvent::tempo(project_.bpm));
    changed();
}

const WtTable* App::table(const std::string& key) {
    if (key.rfind("wt:", 0) == 0) return nullptr;
    auto& tb = tables_[key];
    if (!tb) tb = WtTable::builtin(WavetableKey::name(key));
    return tb.get();
}

void App::publishAudio() {
    // Таблицы, нужные проекту: дорожек, их папок (стрелки переключают без паузы), звуков клипов.
    std::vector<std::string> keys;
    for (const auto& t : project_.tracks) {
        keys.push_back(t.table);
        for (const auto& it : t.folder.items)
            if (it.preset && it.preset->table) keys.push_back(*it.preset->table);
        if (t.home) keys.push_back(t.home->table);
        for (const auto& c : t.clips)
            if (c && c->src) keys.push_back(c->src->table);
    }
    std::map<std::string, std::shared_ptr<WtTable>> tables;
    for (const auto& k : keys)
        if (table(k)) tables[k] = tables_[k];
    audio_->publish(project_, tables, levelRelative_);
}

void App::changed() {
    publishAudio();
    projectChanged();
    bump();
}

// MARK: — дорожки

const Track& App::seq() const {
    switch (channel_) {
    case Channel::track: return track();
    case Channel::mix: return project_.chanSeqs[Project::mixSeq];
    case Channel::master: return project_.chanSeqs[Project::mstSeq];
    }
    return track();
}

void App::selectTrack(int i) {
    if (i < 0 || i >= int(project_.tracks.size())) return;
    selected_ = i;
    channel_ = Channel::track;
    held_.clear();
    viewPage_ = std::min(viewPage_, int(project_.tracks[size_t(i)].pages().size()) - 1);
    bump();
}

void App::selectChannel(Channel c) {
    channel_ = c;
    if (c == Channel::master) heldSources_.clear();
    held_.clear();
    viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
    bump();
}

void App::swipe(int i, bool down) {
    if (i < 0 || i >= int(project_.tracks.size())) return;
    mutatePlay([&](Project& p) {
        auto& t = p.tracks[size_t(i)];
        if (t.mute || t.solo) {
            t.mute = false;
            t.solo = false;
        } else if (down) {
            t.mute = true;
        } else {
            t.solo = true;
        }
    }, true);
}

// MARK: — ход игры

int App::rawTrackPlayPos() const {
    const Track& t = track();
    const int s = selected_;
    const int cell = cell_[size_t(s)];
    if (t.open != t.slot || cell >= SeqData::slots) return -1;
    return playPos_[size_t(s)];
}

int App::openPlayPos() const {
    if (!playing_) return -1;
    if (channel_ == Channel::mix) return playPos_[size_t(SeqData::maxTracks + Project::mixSeq)];
    if (channel_ == Channel::master) return playPos_[size_t(SeqData::maxTracks + Project::mstSeq)];
    return rawTrackPlayPos();
}

void App::playEvent(const SeqOut& e) {
    if (e.kind == SeqOut::Kind::clip) {
        const int t = e.track;
        if (t >= int(project_.tracks.size())) return;
        const int slot = e.a;
        const bool stopped = e.on;
        if (slot >= Params::patternSlots) {
            if (project_.tracks[size_t(t)].stopped != stopped)
                mutatePlay([&](Project& p) { p.tracks[size_t(t)].stopped = stopped; }, false);
            return;
        }
        const bool other = project_.tracks[size_t(t)].slot != slot;
        mutatePlay([&](Project& p) {
            if (p.tracks[size_t(t)].slot != slot) p.tracks[size_t(t)].activate(slot);
            p.tracks[size_t(t)].stopped = stopped;
        }, other);
        if (t == selected_) {
            held_.clear();
            viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
        }
    } else if (e.kind == SeqOut::Kind::ended && playing_) {
        playing_ = false;
        endRecTake();
        trackStart_.fill(0);
        bump();
    }
}

void App::tick(double now) {
    now_ = now;
    tickAutosave(now);
    stepMorphGlide(now);
    tickMeters(now);
    const int before = openPlayPos();
    SeqOut e;
    while (audio_->popOut(e)) {
        switch (e.kind) {
        case SeqOut::Kind::step:
            if (e.track >= 0 && e.track < int(playPos_.size())) playPos_[size_t(e.track)] = e.a;
            if (e.track >= 0 && e.track < Params::maxTracks && e.a >= 0) recPassStep(e.track, e.a);
            break;
        case SeqOut::Kind::trig:
            if (e.track < int(project_.tracks.size()) && project_.audible(e.track)) {
                flashUntil_[size_t(e.track)] = now + 0.09;
                flashMask_ |= 1u << e.track;
            }
            break;
        case SeqOut::Kind::clip:
            if (e.track < int(cell_.size())) cell_[size_t(e.track)] = e.on ? -1 : e.a;
            if (e.track < Params::maxTracks) trackStart_[size_t(e.track)] = e.b;
            playEvent(e);
            break;
        case SeqOut::Kind::ended: playEvent(e); break;
        case SeqOut::Kind::beat:
            beatAccent_ = e.on;
            beatFlash_ = true;
            beatUntil_ = now + 0.09;
            break;
        default: break;
        }
    }
    for (int t = 0; t < Params::maxTracks; ++t)
        if ((flashMask_ >> t & 1u) && now >= flashUntil_[size_t(t)]) flashMask_ &= ~(1u << t);
    if (beatFlash_ && now >= beatUntil_) beatFlash_ = false;
    if (!playing_) {
        flashMask_ = 0;
        beatFlash_ = false;
    }
    // Ход открытого клипа — p-lock играющего шага на ручках: данные перечитываются; мигание — в кадре экрана.
    if (before != openPlayPos()) bump();
}

}  // namespace jambo
