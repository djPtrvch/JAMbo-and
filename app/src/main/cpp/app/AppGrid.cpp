// App: сетка паттернов (порт AppModelGrid.swift и части LiveState.swift, ветка swift JAMbo): лидер (гамма, нота и
// общие FX клипа, запущенного последним), запуск клипов и строк (при игре — командой в колбэк, без игры — сразу),
// ячейки, строки, окно строк. Переход MIX (PREV), аранжировщик, рамка выделения — позже.
#include "App.h"
#include "sequencer/Launcher.h"
#include <algorithm>

namespace jambo {

// MARK: — лидер

void App::started(int t) {
    startOrder_[size_t(t)] = ++startSeq_;
    pendingStarts_.push_back(t);
}

void App::applyPendingLead() {
    if (pendingStarts_.empty()) return;
    const auto list = pendingStarts_;
    pendingStarts_.clear();
    const bool sel = std::find(list.begin(), list.end(), selected_) != list.end();
    applyLead(sel ? selected_ : *std::min_element(list.begin(), list.end()));
}

void App::leadStopped(int t) {
    if (t != lead_) return;
    int best = -1;
    for (int k = 0; k < int(project_.tracks.size()); ++k) {
        if (project_.tracks[size_t(k)].stopped || k == t) continue;
        if (best < 0 || startOrder_[size_t(k)] > startOrder_[size_t(best)]) best = k;
    }
    if (best >= 0) applyLead(best);
}

void App::applyLead(int i) {
    if (i >= int(project_.tracks.size())) return;
    lead_ = i;
    const Track& tr = project_.tracks[size_t(i)];
    if (!tr.clipFilled(tr.slot)) return;
    const Clip c = *tr.clips[size_t(tr.slot)];
    mutatePlay([&](Project& p) {
        if (c.scale) p.scale = *c.scale;
        if (c.root) p.root = *c.root;
        // FX клипа лидера — только общие (Duck); Reverb, Delay, Drive — у каждой дорожки свои.
        if (c.fx)
            for (const auto& [id, v] : *c.fx)
                if (!Project::trackFx(id)) p.fx[id] = v;
    }, true);
}

// MARK: — запуск

void App::queueClip(int i, int k) {
    if (playing_) {
        audio_->push(CoreEvent::launch(i, k));
        return;
    }
    mutatePlay([&](Project& p) {
        if (k == Launcher::stop) {
            p.tracks[size_t(i)].stopped = true;
        } else {
            p.tracks[size_t(i)].activate(k);
            p.tracks[size_t(i)].stopped = false;
        }
    }, true);
    if (k == Launcher::stop) leadStopped(i); else started(i);
    applyPendingLead();
}

void App::queueScene(int k, bool atPass) {
    if (playing_) {
        audio_->push(CoreEvent::scene(k, atPass));
        return;
    }
    mutatePlay([&](Project& p) {
        for (auto& t : p.tracks) {
            if (t.clipFilled(k)) {
                t.activate(k);
                t.stopped = false;
            } else {
                t.stopped = true;
            }
        }
    }, true);
    for (int i = 0; i < int(project_.tracks.size()); ++i)
        if (!project_.tracks[size_t(i)].stopped) started(i);
    applyPendingLead();
}

void App::openClip(int i, int k) {
    if (i >= int(project_.tracks.size()) || project_.tracks[size_t(i)].open == k) return;
    mutatePlay([&](Project& p) { p.tracks[size_t(i)].open = k; }, false);
    if (i == selected_) {
        held_.clear();
        const auto r = project_.tracks[size_t(i)].clipRange(k);
        viewPage_ = r ? r->lower : 0;
    }
}

// MARK: — ячейки

void App::tapCell(int i, int k) {
    if (i >= int(project_.tracks.size())) return;
    selectTrack(i);
    openClip(i, k);
    const Track& t = project_.tracks[size_t(i)];
    if (!t.clipFilled(k)) {
        const int s = project_.scale, r = project_.root;
        edit([&](Project& p) { p.tracks[size_t(i)].putEmptyClip(k, s, r); });
        viewPage_ = 0;
        queueClip(i, k);
    } else if (k == t.slot && !t.stopped) {
        queueClip(i, Launcher::stop);
    } else {
        queueClip(i, k);
    }
    bump();
}

void App::holdCell(int i, int k) {
    if (i >= int(project_.tracks.size())) return;
    selectTrack(i);
    const Track& t = project_.tracks[size_t(i)];
    const bool idle = !playing_ || t.stopped;
    if (!t.clipFilled(k)) {
        if (idle)
            mutatePlay([&](Project& p) {
                p.tracks[size_t(i)].moveSlot(k);
                p.tracks[size_t(i)].stopped = true;
            }, true);
        openClip(i, k);
        bump();
        return;
    }
    if (k != t.slot && idle)
        mutatePlay([&](Project& p) {
            p.tracks[size_t(i)].moveSlot(k);
            p.tracks[size_t(i)].stopped = true;
        }, true);
    openClip(i, k);
    const auto r = project_.tracks[size_t(i)].clipRange(k);
    viewPage_ = r ? r->lower : 0;
    held_.clear();
    bump();
}

void App::copyClip(int i, int k) {
    if (i < int(project_.tracks.size())) clipClipboard_ = project_.tracks[size_t(i)].clipCopy(k);
    bump();
}

void App::pasteClip(int i, int k) {
    if (!clipClipboard_ || i >= int(project_.tracks.size())) return;
    const Clip c = *clipClipboard_;
    edit([&](Project& p) { p.tracks[size_t(i)].setClip(k, c); });
    viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
}

void App::deleteClip(int i, int k) {
    if (i >= int(project_.tracks.size())) return;
    edit([&](Project& p) { p.tracks[size_t(i)].clearClip(k); });
    held_.clear();
    viewPage_ = std::max(0, std::min(viewPage_, int(seq().pages().size()) - 1));
}

void App::dropClip(int ai, int ak, int bi, int bk) {
    if ((ai == bi && ak == bk) || ai >= int(project_.tracks.size()) || bi >= int(project_.tracks.size())) return;
    const auto c = project_.tracks[size_t(ai)].clipCopy(ak);
    if (!c) return;
    edit([&](Project& p) { p.tracks[size_t(bi)].setClip(bk, c); });
    viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
}

// MARK: — строки

void App::addRow() {
    edit([](Project& p) { p.addRow(); });
    gridTop_ = Params::patternSlots;
}

void App::duplicateRow(int k) {
    edit([&](Project& p) { p.duplicateRow(k); });
    if (playing_) audio_->push(CoreEvent::adoptPlay());
}

void App::moveRow(int a, int b) {
    if (a == b) return;
    edit([&](Project& p) { p.moveRow(a, b); });
    if (playing_) audio_->push(CoreEvent::adoptPlay());
}

void App::deleteRow(int k) {
    edit([&](Project& p) { p.deleteRow(k); });
    if (playing_) audio_->push(CoreEvent::adoptPlay());
    held_.clear();
    viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
}

void App::copyRow(int k) {
    rowClipboard_ = project_.copyRow(k);
    bump();
}

void App::pasteRow(int k) {
    if (!rowClipboard_) return;
    const auto c = *rowClipboard_;
    edit([&](Project& p) { p.pasteRow(k, c); });
}

void App::setScene(int k, const std::string& name, int repeats) {
    edit([&](Project& p) { p.setScene(k, name, repeats); });
}

void App::setGridOption(GridOption o, int v) {
    edit([&](Project& p) {
        switch (o) {
        case GridOption::start: p.arrStart = v; break;
        case GridOption::endStop: p.arrEndStop = v != 0; break;
        case GridOption::plusCopy: p.plusCopy = v != 0; break;
        case GridOption::arrowSwitch: p.arrowSwitch = v != 0; break;
        case GridOption::rowLoop: p.rowLoop = !p.rowLoop; break;
        }
    });
}

// MARK: — окно строк

App::GridWindow App::gridWindow() const {
    GridWindow w;
    const int n = project_.visibleRows();
    const bool plus = project_.canAddRow();
    const int total = n + (plus ? 1 : 0);
    const int win = Params::gridRows;
    w.maxTop = std::max(0, total - win);
    w.top = std::min(std::max(0, gridTop_), w.maxTop);
    for (int r = 0; r < win; ++r) {
        const int k = w.top + r;
        if (k < n) {
            w.rows.push_back(k);
        } else {
            w.rows.push_back(-1);
            if (plus && k == n) w.plusRow = r;
        }
    }
    return w;
}

void App::gridArrow(int dir) {
    const GridWindow w = gridWindow();
    if (project_.arrowSwitch && playing_) {
        const int cur = sceneQueued_ >= 0 ? sceneQueued_ : std::max(0, sceneRow_);
        const int k = std::min(project_.visibleRows() - 1, std::max(0, cur + dir));
        if (k != cur) queueScene(k, true);
        if (k < w.top) gridTop_ = k;
        else if (k >= w.top + Params::gridRows) gridTop_ = k - Params::gridRows + 1;
        bump();
        return;
    }
    gridTop_ = std::min(w.maxTop, std::max(0, w.top + dir));
    bump();
}

void App::setGridTop(int top) {
    gridTop_ = top;
    bump();
}

void App::togglePatterns() {
    patternView_ = !patternView_;
    if (patternView_) openClip(selected_, track().slot);
    bump();
}

double App::cellProgress(int i, int k) const {
    if (i >= int(project_.tracks.size()) || !project_.tracks[size_t(i)].clipFilled(k)) return 0;
    const int tick = int(audio_->clock().nextTick) - 1;
    const int len = std::max(1, project_.tracks[size_t(i)].clips[size_t(k)]->passTicks());
    const int n = ((tick - trackStart_[size_t(i)]) % len + len) % len;
    return double(n + 1) / double(len);
}

std::pair<int, double> App::scenePass() const {
    const int tick = int(audio_->clock().nextTick) - 1;
    const int len = std::max(1, sceneLen_);
    const int n = std::max(0, tick - sceneStart_);
    return {n / len, double(n % len + 1) / double(len)};
}

void App::liveStopped() {
    queued_.fill(Launcher::none);
    trackStart_.fill(0);
    sceneRow_ = -1;
    sceneQueued_ = -1;
}

}  // namespace jambo
