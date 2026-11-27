// Правка карты варпинга редактора (AppModelWarp.swift и Warp.swift).
#include "App.h"
#include <limits>

namespace jambo {

void App::setSamplerSettings(SliceMap map, bool follow) {
    if (map == sliceMap_ && follow == warpFollow_) return;
    sliceMap_ = map;
    warpFollow_ = follow;
    publishAudio();
    bump();
}

WarpInfo App::editorWarpInfo() const {
    const auto it = track().warps.find(editorKey());
    return it == track().warps.end() ? WarpInfo{} : it->second;
}

double App::editorWarpBpm(const SampleRef& sample) const {
    const auto info = editorWarpInfo();
    return WarpMap::bpm(&info, sample && !info.bpm ? sample->loopBpm() : 0,
                        warpFollow_ ? std::optional<double>(project_.bpm) : std::nullopt);
}

void App::editWarp(WarpEdit command, int index, double a, double b) {
    const auto key = editorKey();
    const auto sample = sampleData(key);
    if (!sample || sample->frames() <= 0 || !std::isfinite(a) || !std::isfinite(b)) return;
    auto info = editorWarpInfo();
    auto& marks = info.marks;
    const int frames = sample->frames();
    const auto bpmForNewMarks = [&] {
        if (warpFollow_ && !info.isOff()) return project_.bpm;
        return WarpMap::bpm(&info, info.bpm ? 0 : sample->loopBpm());
    };

    switch (command) {
    case WarpEdit::enter:
        if (previewOn_) editorPreview(-1, 0);
        if (warpFollow_ && !info.bpm) info.bpm = project_.bpm;
        break;
    case WarpEdit::bypass:
        info.off = info.isOff() ? std::nullopt : std::optional<bool>(true);
        break;
    case WarpEdit::bpm:
        info.bpm = a <= 0 ? std::nullopt : std::optional<double>(std::clamp(a, WarpMap::bpmMin, WarpMap::bpmMax));
        break;
    case WarpEdit::automatic:
        marks = WarpMap::autoMarks(sample->onsets().at, frames, bpmForNewMarks(), a);
        break;
    case WarpEdit::clear:
        marks.clear();
        break;
    case WarpEdit::start:
    case WarpEdit::add: {
        const double at = command == WarpEdit::start ? 0 : std::clamp(a, 0.0, 1.0);
        const double p = at * frames;
        if (std::any_of(marks.begin(), marks.end(), [&](const auto& m) {
                return std::abs(m.at * frames - p) < WarpMap::minFrames;
            })) return;
        const WarpMap map(info, frames, bpmForNewMarks());
        const auto it = std::upper_bound(marks.begin(), marks.end(), at,
                                         [](double f, const auto& m) { return f < m.at; });
        marks.insert(it, {at, map.beat(p)});
        break;
    }
    case WarpEdit::move:
    case WarpEdit::place: {
        if (index < 0 || index >= int(marks.size())) return;
        const double max = std::numeric_limits<double>::max();
        const auto* prev = index > 0 ? &marks[size_t(index - 1)] : nullptr;
        const auto* next = index + 1 < int(marks.size()) ? &marks[size_t(index + 1)] : nullptr;
        const double lo = prev ? prev->beat + WarpMap::minBeat : -max;
        const double hi = next ? next->beat - WarpMap::minBeat : max;
        if (lo > hi) return;
        auto& mark = marks[size_t(index)];
        if (command == WarpEdit::place) {
            const double gap = WarpMap::minFrames / frames;
            const double minAt = prev ? prev->at + gap : 0;
            const double maxAt = next ? next->at - gap : 1;
            if (minAt > maxAt) return;
            mark.at = std::clamp(a, minAt, maxAt);
        }
        mark.beat = std::clamp(b, lo, hi);
        break;
    }
    case WarpEdit::remove:
        if (index < 0 || index >= int(marks.size())) return;
        marks.erase(marks.begin() + index);
        break;
    }

    editTrack([&](Track& t) {
        if (info.isEmpty()) t.warps.erase(key);
        else t.warps[key] = info;
    });
}

void App::editorWarpPreview() {
    const auto key = editorKey();
    if (!sampleData(key)) return;
    if (previewOn_) editorPreview(-1, 0);
    else {
        previewOn_ = true;
        audio_->push(CoreEvent::warpPreview(selected_, SoundKey::hash(key)));
        bump();
    }
}
}  // namespace jambo
