// Запись поворотов ручек на таймлайне (AppModelArrangeAuto.swift).
#include "App.h"
#include <algorithm>

namespace jambo {

bool App::recordArrangeAutoPoint(int slot, const ParamDef& param, double norm) {
    if (!(arrangeRecorder_.active() || (recOn_ && playing_ && arrangePlaying_ && arrangeAuto_)) ||
        channel_ != Channel::track || fxMode() || modMode() || !held_.empty() || morph_) return false;
    const int target = ArrAutoTarget::index(param.id);
    if (target < 0) return false;
    const int key = selected_ * ArrAutoTarget::count + target;
    auto& params = project_.tracks[size_t(selected_)].params;
    const auto existing = params.find(param.id);
    const auto saved = existing != params.end() ? std::optional<double>(existing->second) : std::nullopt;
    auto& take = arrangeAutoTakes_.try_emplace(key, ArrangeAutoTake{slot, param.id, saved}).first->second;
    const double x = arrangePositionX();
    if (!take.segments.back().empty() && x < take.segments.back().back().x - 0.5) take.segments.emplace_back();
    auto& segment = take.segments.back();
    const ArrAutoPoint pt{x, std::clamp(norm, 0.0, 1.0)};
    if (segment.size() < ArrAutoLane::maxPoints) segment.push_back(pt);
    else segment.back() = pt;
    take.lastAt = now_;
    holdArrangeAuto(key, true);
    params[param.id] = param.fromNorm(norm);
    publishAudio();
    bump();
    return true;
}

void App::touchArrangeAuto(int slot, bool down) {
    if (down) {
        if (channel_ != Channel::track || fxMode() || modMode()) return;
        const auto* def = param(slot);
        const int target = def ? ArrAutoTarget::index(def->id) : -1;
        if (target < 0) return;
        const int key = selected_ * ArrAutoTarget::count + target;
        arrangeAutoTouches_[slot] = key;
        if (arrangePlaying_) holdArrangeAuto(key, true);
        return;
    }
    const auto i = arrangeAutoTouches_.find(slot);
    if (i == arrangeAutoTouches_.end()) return;
    const int key = i->second;
    arrangeAutoTouches_.erase(i);
    if (!arrangeRecorder_.active()) finishArrangeAutoKey(key);
    holdArrangeAuto(key, false);
}

void App::holdArrangeAuto(int key, bool on) {
    const bool changed = on ? arrangeAutoHeld_.insert(key).second : arrangeAutoHeld_.erase(key) > 0;
    if (changed) audio_->push(CoreEvent::autoHold(key / ArrAutoTarget::count, key % ArrAutoTarget::count, on));
}

void App::finishArrangeAutoKey(int key, std::optional<double> end) {
    const auto i = arrangeAutoTakes_.find(key);
    if (i == arrangeAutoTakes_.end()) return;
    auto take = std::move(i->second);
    arrangeAutoTakes_.erase(i);
    const int t = key / ArrAutoTarget::count;
    if (t >= int(project_.tracks.size())) return;
    auto& last = take.segments.back();
    if (end && !last.empty() && *end > last.back().x) last.push_back({*end, last.back().y});
    auto& params = project_.tracks[size_t(t)].params;
    if (take.original) params[take.id] = *take.original;
    else params.erase(take.id);
    edit([&](Project& p) {
        for (const auto& segment : take.segments)
            if (!segment.empty()) p.tracks[size_t(t)].recordAuto(take.id, segment);
    });
    publishAudio();
    bump();
}

void App::finishArrangeAuto(bool all, std::optional<double> end) {
    if (!all && arrangeRecorder_.active()) return;
    std::vector<int> keys;
    for (const auto& [key, take] : arrangeAutoTakes_) {
        const auto touch = arrangeAutoTouches_.find(take.slot);
        const bool touching = touch != arrangeAutoTouches_.end() && touch->second == key;
        if (all || (!touching && now_ - take.lastAt >= 0.30)) keys.push_back(key);
    }
    for (int key : keys) {
        finishArrangeAutoKey(key, end);
        holdArrangeAuto(key, false);
    }
    if (all && !playing_) {
        while (!arrangeAutoHeld_.empty()) holdArrangeAuto(*arrangeAutoHeld_.begin(), false);
        arrangeAutoTouches_.clear();
    }
}

}  // namespace jambo
