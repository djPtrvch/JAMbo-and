// App: запись нот с REC (порт части AppModelPiano.swift, ветка swift JAMbo): во время игры нота клавиатуры (и MIDI)
// ложится на шаг, который слышен сейчас (QUANTIZE — ближайший); длина — по времени удержания; всё, что записано за
// один проход, — одна запись UNDO (дубль). OVERDUB OFF — шаги, по которым проходит запись, очищаются.
#include "App.h"
#include "engine/AudioCore.h"
#include "model/Recording.h"

namespace jambo {

namespace {
/// Правка играющего клипа дорожки (запись с REC), что бы ни было открыто.
void inPlaying(Track& t, const std::function<void(Track&)>& change) {
    const int saved = t.open;
    t.open = t.slot;
    change(t);
    t.open = saved;
}
}  // namespace

void App::toggleRec() {
    recOn_ = !recOn_;
    endRecTake();
    bump();
}

std::optional<int> App::heardTick() const {
    const auto c = audio_->clock();
    if (!c.playing) return std::nullopt;
    const double frame = c.blockFrame + (audio_->heardNow() - c.blockHost) * AudioCore::sampleRate;
    return RecordClock::tick(frame, int(c.nextTick), c.nextFrame, c.stepFrames, quantize_);
}

std::optional<int> App::record(int ti, const std::vector<int>& notes, std::optional<double> velocity, std::optional<int> tick) {
    const auto k = tick ? tick : heardTick();
    if (!k) return std::nullopt;
    const bool merge = lastRec_.first == ti && lastRec_.second == *k;
    lastRec_ = {ti, *k};
    const Track& tr = project_.tracks[size_t(ti)];
    const auto& clip = tr.clips[size_t(tr.slot)];
    if (!clip) return std::nullopt;
    // Скорость клипа: позиция прохода на тике k.
    const int index = clip->playIndex(clip->localStep(*k - trackStart_[size_t(ti)]));
    const bool overdub = overdub_;
    takeTracks_.insert(ti);
    takeWritten_.insert(ti * 1000 + index);
    writeTake(ti, [&](Track& t) { t.recordNotes(index, notes, merge, overdub, velocity); });
    return index;
}

void App::recordArp() {
    ArpOut note;
    while (audio_->popArp(note)) {
        if (!recOn_ || !playing_ || !note.playing || channel_ != Channel::track || !held_.empty()) continue;
        if (note.track < 0 || note.track >= int(project_.tracks.size())) continue;
        const double stepFrames = std::max(1.0, note.stepFrames);
        const int tick = RecordClock::tick(note.frame, note.nextTick, note.nextFrame, stepFrames, quantize_);
        int& last = lastArpTick_[size_t(note.track)];
        if (last == tick) continue;
        last = tick;
        const auto velocity = note.velocity < 1 ? std::optional<double>(note.velocity) : std::nullopt;
        if (const auto index = record(note.track, {note.note}, velocity, tick)) {
            const int length = RecordClock::length(note.gateFrames, stepFrames);
            writeTake(note.track, [&](Track& t) { t.recordLength(*index, length); });
        }
    }
}

void App::writeTake(int ti, const std::function<void(Track&)>& change) {
    if (recTake_ >= 0 && recTake_ == history_.serial()) {
        // Продолжение дубля: без новой записи UNDO.
        inPlaying(project_.tracks[size_t(ti)], change);
        changed();
        return;
    }
    const int before = history_.serial();
    edit([&](Project& p) { inPlaying(p.tracks[size_t(ti)], change); });
    recTake_ = history_.serial() != before ? history_.serial() : -1;
    if (recTake_ < 0) {
        takeTracks_.clear();
        takeWritten_.clear();
    }
}

void App::endRecTake() {
    recTake_ = -1;
    takeTracks_.clear();
    takeWritten_.clear();
    lastRec_ = {-1, -1};
    lastArpTick_.fill(-1);
}

void App::recPassStep(int ti, int pos) {
    if (overdub_ || recTake_ < 0 || recTake_ != history_.serial()) return;
    if (!takeTracks_.count(ti) || takeWritten_.count(ti * 1000 + pos)) return;
    if (ti >= int(project_.tracks.size())) return;
    const Track& tr = project_.tracks[size_t(ti)];
    const auto& c = tr.clips[size_t(tr.slot)];
    if (!c) return;
    const Step* s = c->step(pos);
    if (!s || (!s->on && !s->sound && s->locks.empty())) return;
    inPlaying(project_.tracks[size_t(ti)], [&](Track& t) { t.clearPassStep(pos); });
    changed();
}

}  // namespace jambo
