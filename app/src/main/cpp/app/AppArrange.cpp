// Связь таймлайна с AppModel, транспортом и выбранным для правки блоком (AppModelArrange.swift).
#include "App.h"
#include <algorithm>
#include <cmath>

namespace jambo {

double App::arrangePositionX() const {
    if (!playing_) return arrangeCursor_;
    return arrangeLive_ + std::clamp((now_ - arrangeLiveTime_) * runningBpm() / 15, 0.0, 0.95);
}

void App::toggleArrange() {
    if (arrangeView_) {
        closeArrange();
    } else {
        patternView_ = false;
        browser_.reset();
        closeMasterWindow();
        mixerOpen_ = false;
        eqView_ = false;
        syncSpectra();
        arrangeView_ = true;
        syncArrangeEditBlock();
        if (arrangeReferenceB()) publishAudio();
    }
    bump();
}

void App::closeArrange() {
    const bool wasB = arrangeReferenceB();
    arrangeView_ = false;
    arrangeSelection_.reset();
    syncArrangeEditBlock();
    if (wasB) publishAudio();
}

void App::prepareArrangePlay() {
    arrangePlaying_ = arrangeView_;
    const auto& arrangement = project_.arrange;
    int from = arrangeCursor_;
    if (arrangePlaying_) {
        if (arrangement.loop && arrangement.hasLoop()) from = arrangement.loopFrom;
        else if (project_.arrangeEnd() > 0 && from >= project_.arrangeEnd()) from = arrangeCursor_ = 0;
    }
    if (!arrangePlaying_ && arrangeRecordArmed_ && arrangeRecordFromStart_) from = arrangeCursor_ = 0;
    arrangeLive_ = from;
    arrangeLiveTime_ = now_;
    arrangeTick_ = -1;
    syncArrangeRecording();
    audio_->push(CoreEvent::arrange(arrangePlaying_, from));
    syncArrangeEditBlock();
}

void App::seekArrange(int step) {
    arrangeCursor_ = project_.arrange.snapped(std::clamp(step, 0, 1'000'000));
    if (playing_) audio_->push(CoreEvent::seekArrange(arrangeCursor_));
    syncArrangeEditBlock();
    bump();
}

bool App::validArrangeBlock(int t, int b) const {
    return t >= 0 && t < int(project_.tracks.size()) &&
        b >= 0 && b < int(project_.tracks[size_t(t)].blocks.size());
}

void App::syncArrangeEditBlock() {
    if (arrangeSelection_ && !validArrangeBlock(arrangeSelection_->first, arrangeSelection_->second))
        arrangeSelection_.reset();
    std::optional<int> wanted;
    if (arrangeView_ && channel_ == Channel::track && selected_ < int(project_.tracks.size())) {
        if (arrangeSelection_ && arrangeSelection_->first == selected_) {
            wanted = arrangeSelection_->second;
        } else {
            const auto& blocks = track().blocks;
            const int step = arrangePosition();
            const auto pos = std::find_if(blocks.begin(), blocks.end(),
                [step](const ArrBlock& b) { return b.start <= step && step < b.end(); });
            if (pos != blocks.end()) wanted = int(pos - blocks.begin());
        }
    }
    bool differs = false;
    for (int t = 0; t < int(project_.tracks.size()); ++t) {
        auto& block = project_.tracks[size_t(t)].editBlock;
        const auto next = t == selected_ ? wanted : std::nullopt;
        if (block != next) {
            block = next;
            differs = true;
        }
    }
    if (!differs) return;
    held_.clear();
    viewPage_ = std::clamp(viewPage_, 0, int(seq().pages().size()) - 1);
    bump();
}

void App::selectArrangeBlock(int t, int b) {
    if (t < 0 || t >= int(project_.tracks.size())) return;
    const auto next = validArrangeBlock(t, b) ? std::optional<Project::BlockRef>{{t, b}} : std::nullopt;
    arrangeSelection_ = next;
    selectTrack(t);
}

int App::arrangeCarryLength(int t, int clip) const {
    if (t < 0 || t >= int(project_.tracks.size()) || !project_.tracks[size_t(t)].clipFilled(clip)) return 0;
    const int reps = arrangeRepeats_ && clip < int(project_.scenes.size()) ? std::max(1, project_.scenes[size_t(clip)].repeats) : 1;
    return project_.tracks[size_t(t)].clipSteps(clip) * reps;
}

void App::addArrangeBlock(int t, int clip, int step, bool insert, std::optional<int> length) {
    if (t < 0 || t >= int(project_.tracks.size()) || !project_.tracks[size_t(t)].clipFilled(clip)) return;
    const int safe = std::clamp(step, 0, 1'000'000);
    const int at = insert ? safe : project_.arrange.snapped(safe);
    int result = -1;
    edit([&](Project& p) {
        auto& tr = p.tracks[size_t(t)];
        result = insert ? tr.insertBlock(clip, at, length) : tr.addBlock(clip, at, length);
    });
    if (result >= 0) selectArrangeBlock(t, result);
}

void App::moveArrangeBlock(int t, int b, int step) {
    if (!validArrangeBlock(t, b)) return;
    int result = b;
    const int at = project_.arrange.snapped(std::clamp(step, 0, 1'000'000));
    edit([&](Project& p) { result = p.tracks[size_t(t)].moveBlock(b, at); });
    selectArrangeBlock(t, result);
}

void App::resizeArrangeBlock(int t, int b, int begin, int end) {
    if (!validArrangeBlock(t, b)) return;
    int result = b;
    const int lo = project_.arrange.snapped(std::clamp(begin, 0, 1'000'000));
    const int hi = project_.arrange.snapped(std::clamp(end, 0, 1'000'000));
    const int minimum = project_.arrange.snap ? project_.arrange.gridSteps() : 1;
    edit([&](Project& p) { result = p.tracks[size_t(t)].resizeBlock(b, lo, std::max(hi, lo + minimum)); });
    selectArrangeBlock(t, result);
}

void App::duplicateArrangeBlock(int t, int b) {
    if (!validArrangeBlock(t, b)) return;
    int result = b;
    edit([&](Project& p) { result = p.tracks[size_t(t)].duplicateBlock(b); });
    selectArrangeBlock(t, result);
}

void App::deleteArrangeBlock(int t, int b) {
    if (!validArrangeBlock(t, b)) return;
    arrangeSelection_.reset();
    edit([&](Project& p) { p.tracks[size_t(t)].deleteBlock(b); });
}

void App::splitArrangeBlock(const std::vector<Project::BlockRef>& group) {
    const int at = arrangePosition();
    std::set<int> tracks;
    if (group.size() >= 2) {
        for (const auto& [t, b] : group) {
            if (!validArrangeBlock(t, b)) continue;
            const auto& block = project_.tracks[size_t(t)].blocks[size_t(b)];
            if (block.start < at && at < block.end()) tracks.insert(t);
        }
    } else {
        if (!arrangeSelection_ && channel_ != Channel::track) return;
        const int t = arrangeSelection_ ? arrangeSelection_->first : selected_;
        if (t < 0 || t >= int(project_.tracks.size())) return;
        tracks.insert(t);
    }
    if (tracks.empty()) return;
    arrangeSelection_.reset();
    edit([&](Project& p) {
        for (int t : tracks) p.tracks[size_t(t)].splitArrange(at);
    });
    syncArrangeEditBlock();
    bump();
}

void App::copyArrangeBlock(int t, int b) {
    if (!validArrangeBlock(t, b)) return;
    arrangeBlockClipboard_ = project_.tracks[size_t(t)].blocks[size_t(b)];
    bump();
}

void App::pasteArrangeBlock(int t, int b, int step) {
    if (!arrangeBlockClipboard_ || t < 0 || t >= int(project_.tracks.size())) return;
    int result = -1;
    edit([&](Project& p) {
        auto& lane = p.tracks[size_t(t)];
        if (validArrangeBlock(t, b)) {
            result = lane.pasteBlock(b, *arrangeBlockClipboard_);
        } else {
            auto copy = *arrangeBlockClipboard_;
            copy.start = p.arrange.snapped(std::clamp(step, 0, 1'000'000));
            result = lane.placeBlock(copy);
        }
    });
    if (result >= 0) selectArrangeBlock(t, result);
}

void App::arrangeTrackClipboard(int t, int command) {
    if (t < 0 || t >= int(project_.tracks.size())) return;
    switch (command) {
    case 0:
        arrangeTrackClipboard_ = project_.tracks[size_t(t)].blocks;
        bump();
        break;
    case 1:
        if (arrangeTrackClipboard_) {
            arrangeSelection_.reset();
            edit([&](Project& p) { p.pasteBlocks(t, *arrangeTrackClipboard_); });
        }
        break;
    case 2: edit([&](Project& p) { p.tracks[size_t(t)].duplicateBlocks(); }); break;
    }
}

std::vector<Project::BlockRef> App::arrangeGroup(const std::vector<Project::BlockRef>& refs, int command, int delta) {
    const std::set<Project::BlockRef> unique(refs.begin(), refs.end());
    std::vector<Project::BlockRef> group;
    for (const auto& ref : unique)
        if (validArrangeBlock(ref.first, ref.second)) group.push_back(ref);
    if (group.size() < 2) return group;
    if (command == 3) {
        splitArrangeBlock(group);
        return {};
    }
    arrangeSelection_.reset();
    std::vector<Project::BlockRef> result;
    edit([&](Project& p) {
        if (command == 0) result = p.moveBlocks(group, std::clamp(delta, -1'000'000, 1'000'000));
        else if (command == 1) result = p.duplicateBlocks(group);
        else if (command == 2) p.deleteBlocks(group);
    });
    return result;
}

void App::setArrangeGrid(int grid) {
    edit([grid](Project& p) { p.arrange.grid = std::clamp(grid, 0, 3); });
}

void App::toggleArrangeSnap() {
    edit([](Project& p) { p.arrange.snap = !p.arrange.snap; });
}

void App::setArrangeLoop(int begin, int end) {
    edit([&](Project& p) {
        p.setLoop(p.arrange.snapped(std::clamp(begin, 0, 1'000'000)),
                  p.arrange.snapped(std::clamp(end, 0, 1'000'000)));
    });
}

void App::toggleArrangeLoop() {
    if (project_.arrange.hasLoop())
        edit([](Project& p) { p.arrange.loop = !p.arrange.loop; });
}

void App::toggleArrangeSteps() {
    edit([](Project& p) { p.arrange.hideSteps = !p.arrange.hideSteps; });
    if (!project_.arrange.hideSteps) arrangeAuto_ = false;
    if (!arrangeAuto_) syncArrangeRecording();
}

void App::sendRowToArrange(int row, std::optional<int> step) {
    if (row < 0 || row >= project_.visibleRows()) return;
    const int at = project_.arrange.snapped(step.value_or(arrangeCursor_));
    edit([&](Project& p) { p.rowToArrange(row, at, arrangeRepeats_); });
}

void App::sendClipToArrange(int track, int row) {
    if (track < 0 || track >= int(project_.tracks.size()) || row < 0 || row >= project_.visibleRows()) return;
    const int at = project_.arrange.snapped(arrangeCursor_);
    edit([&](Project& p) {
        const int repeats = arrangeRepeats_ && row < int(p.scenes.size()) ? p.scenes[size_t(row)].repeats : 1;
        p.placeRow(row, {track}, repeats, at);
    });
}

void App::setArrangeTrackColor(int track, int color) {
    edit([&](Project& p) { p.setTrackColor(track, color); });
}

void App::addArrangeSection(int step) {
    edit([&](Project& p) { p.addMarker(p.arrange.snapped(step), "SECTION"); });
}

void App::resizeArrangeSection(int index, int step, bool left) {
    edit([&](Project& p) {
        if (left) p.setMarkerStart(index, p.arrange.snapped(step));
        else p.setMarkerEnd(index, p.arrange.snapped(step));
    });
}

void App::renameArrangeSection(int index, const std::string& name) {
    edit([&](Project& p) { p.renameMarker(index, name); });
}

void App::deleteArrangeSection(int index) {
    edit([&](Project& p) { p.deleteMarker(index); });
}

void App::toggleArrangeAuto() {
    arrangeAuto_ = !arrangeAuto_;
    if (arrangeAuto_ && !project_.arrange.hideSteps)
        edit([](Project& p) { p.arrange.hideSteps = true; });
    syncArrangeRecording();
    bump();
}

void App::setArrangeAuto(int t, const std::string& id, const std::vector<ArrAutoPoint>& points, bool draw) {
    if (t < 0 || t >= int(project_.tracks.size()) || ArrAutoTarget::index(id) < 0 ||
        points.size() > ArrAutoLane::maxPoints) return;
    std::vector<ArrAutoPoint> checked;
    checked.reserve(points.size());
    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return;
        checked.push_back({std::clamp(point.x, 0.0, 1'000'000.0), std::clamp(point.y, 0.0, 1.0)});
    }
    if (draw && !checked.empty()) {
        std::stable_sort(checked.begin(), checked.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
        edit([&](Project& p) { p.tracks[size_t(t)].recordAuto(id, checked, 0.005); });
    } else if (!draw) {
        edit([&](Project& p) { p.tracks[size_t(t)].setAuto(id, checked); });
    }
}

void App::deleteArrangeAuto(int t, const std::string& id) {
    if (t < 0 || t >= int(project_.tracks.size())) return;
    edit([&](Project& p) { p.tracks[size_t(t)].deleteAuto(id); });
}

void App::clearArrangeAuto(int t) {
    if (t < 0 || t >= int(project_.tracks.size())) return;
    edit([&](Project& p) { p.tracks[size_t(t)].clearAllAutos(); });
}

void App::toggleArrangeRecord() {
    arrangeRecordArmed_ = !arrangeRecordArmed_;
    syncArrangeRecording();
    bump();
}

void App::syncArrangeRecording() {
    const bool want = arrangeRecordArmed_ && playing_ && !arrangePlaying_;
    if (want && !arrangeRecorder_.active()) {
        std::vector<ArrRecorder::State> states(Params::maxTracks);
        std::vector<bool> audible(Params::maxTracks, false);
        for (int t = 0; t < int(project_.tracks.size()); ++t) {
            const auto& track = project_.tracks[size_t(t)];
            const int slot = !track.stopped && track.clipFilled(track.slot) ? track.slot : -1;
            const int offset = std::max(0, std::max(0, arrangeTick_) - trackStart_[size_t(t)]);
            states[size_t(t)] = std::pair{slot, offset};
            audible[size_t(t)] = project_.audible(t);
        }
        arrangeRecorder_.begin(arrangeLive_, states, audible);
    } else if (!want && arrangeRecorder_.active()) {
        const double end = arrangeRecorder_.pos() + 1;
        const auto takes = arrangeRecorder_.finish();
        beginGesture();
        if (!takes.empty()) {
            edit([&](Project& p) {
                p.applyRecording(takes, arrangeRecorder_.played(), arrangeRecorder_.fired(),
                    arrangeRecorder_.heard(), arrangeRecorder_.live());
            });
        }
        finishArrangeAuto(true, end);
        endGesture();
        for (auto& [id, held] : keyHolds_) held.arrangeNote.reset();
    }
    if (!want && !(recOn_ && playing_ && arrangePlaying_ && arrangeAuto_)) finishArrangeAuto(true);
}

void App::recordArrangeAudible() {
    if (!arrangeRecorder_.active()) return;
    std::vector<bool> audible(Params::maxTracks, false);
    for (int t = 0; t < int(project_.tracks.size()); ++t) audible[size_t(t)] = project_.audible(t);
    arrangeRecorder_.audible(audible, arrangeLive_);
}

}  // namespace jambo
