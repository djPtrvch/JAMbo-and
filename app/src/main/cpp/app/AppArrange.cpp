// Связь таймлайна с AppModel, транспортом и выбранным для правки блоком (AppModelArrange.swift).
#include "App.h"
#include <algorithm>
#include <cmath>

namespace jambo {

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
    }
    bump();
}

void App::closeArrange() {
    arrangeView_ = false;
    arrangeSelection_.reset();
    syncArrangeEditBlock();
}

void App::prepareArrangePlay() {
    arrangePlaying_ = arrangeView_;
    const auto& arrangement = project_.arrange;
    const int from = arrangement.loop && arrangement.hasLoop() ? arrangement.loopFrom
        : arrangeCursor_ < project_.arrangeEnd() ? arrangeCursor_ : 0;
    if (arrangePlaying_) arrangeLive_ = from;
    audio_->push(CoreEvent::arrange(arrangePlaying_, arrangePlaying_ ? from : 0));
    syncArrangeEditBlock();
}

void App::seekArrange(int step) {
    arrangeCursor_ = project_.arrange.snapped(std::clamp(step, 0, 1'000'000));
    if (arrangePlaying_) audio_->push(CoreEvent::seekArrange(arrangeCursor_));
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

void App::addArrangeBlock(int t, int clip, int step, bool insert) {
    if (t < 0 || t >= int(project_.tracks.size()) || !project_.tracks[size_t(t)].clipFilled(clip)) return;
    const int at = project_.arrange.snapped(std::clamp(step, 0, 1'000'000));
    int result = -1;
    edit([&](Project& p) {
        auto& tr = p.tracks[size_t(t)];
        result = insert ? tr.insertBlock(clip, at) : tr.addBlock(clip, at);
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

void App::splitArrangeBlock() {
    if (!arrangeSelection_ && channel_ != Channel::track) return;
    const int t = arrangeSelection_ ? arrangeSelection_->first : selected_;
    if (t < 0 || t >= int(project_.tracks.size())) return;
    const int at = arrangePosition();
    arrangeSelection_.reset();
    edit([&](Project& p) { p.tracks[size_t(t)].splitArrange(at); });
    syncArrangeEditBlock();
    bump();
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
}

void App::sendRowToArrange(int row, std::optional<int> step) {
    if (row < 0 || row >= project_.visibleRows()) return;
    edit([&](Project& p) { p.rowToArrange(row, step); });
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
    bump();
}

void App::setArrangeAuto(int t, const std::string& id, const std::vector<ArrAutoPoint>& points) {
    if (t < 0 || t >= int(project_.tracks.size()) || ArrAutoTarget::index(id) < 0 ||
        points.size() > ArrAutoLane::maxPoints) return;
    std::vector<ArrAutoPoint> checked;
    checked.reserve(points.size());
    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return;
        checked.push_back({std::clamp(point.x, 0.0, 1'000'000.0), std::clamp(point.y, 0.0, 1.0)});
    }
    edit([&](Project& p) { p.tracks[size_t(t)].setAuto(id, checked); });
}

void App::deleteArrangeAuto(int t, const std::string& id) {
    if (t < 0 || t >= int(project_.tracks.size())) return;
    edit([&](Project& p) { p.tracks[size_t(t)].deleteAuto(id); });
}

void App::clearArrangeAuto(int t) {
    if (t < 0 || t >= int(project_.tracks.size())) return;
    edit([&](Project& p) { p.tracks[size_t(t)].clearAllAutos(); });
}

}  // namespace jambo
