// Измеритель справа (порт MeterState.swift, ветка swift JAMbo).
#include "MeterState.h"
#include <algorithm>
#include <cmath>

namespace jambo {

void MeterState::tick(const Levels& lv, double now) {
    const int n = channels;
    std::fill(peak_.begin(), peak_.end(), 0.f);
    std::fill(newDuck_.begin(), newDuck_.end(), 0.f);
    count_ = lv.read(count_, [&](const float* b) {
        for (int c = 0; c < n; ++c) {
            const float l = Levels::value(b, c, 0);
            const float r = Levels::value(b, c, 1);
            const float d = Levels::value(b, c, 2);
            if (l > peak_[size_t(c * 2)]) peak_[size_t(c * 2)] = l;
            if (r > peak_[size_t(c * 2 + 1)]) peak_[size_t(c * 2 + 1)] = r;
            if (d > newDuck_[size_t(c)]) newDuck_[size_t(c)] = d;
        }
    });
    for (int c = 0; c < n; ++c) {
        duck[size_t(c)] = newDuck_[size_t(c)];
        for (int k = 0; k < 2; ++k) {
            const size_t i = size_t(c * 2 + k);
            const float p = peak_[i];
            disp[i] = std::max(p, disp[i] * 0.9f);
            if (p >= hold[i]) {
                hold[i] = p;
                holdAt_[i] = now;
            } else if (now - holdAt_[i] > 1.5) {
                hold[i] *= 0.95f;
            }
            if (p > maxPeak[size_t(c)]) maxPeak[size_t(c)] = p;
        }
        if (std::max(peak_[size_t(c * 2)], peak_[size_t(c * 2 + 1)]) >= over) clipUntil_[size_t(c)] = now + 2;
    }
}

void MeterState::show(int ch, Levels& lv) {
    if (ch == shown_) return;
    shown_ = ch;
    maxPeak[size_t(ch)] = 0;
    lv.loudness.source.store(ch);
}

float MeterState::framePeak(int ch) const { return std::max(peak_[size_t(ch * 2)], peak_[size_t(ch * 2 + 1)]); }

void MeterState::resetPeak(int ch) {
    if (ch >= 0 && ch < channels) maxPeak[size_t(ch)] = 0;
}

bool MeterState::clip(int ch, double now) const { return ch >= 0 && ch < channels && now < clipUntil_[size_t(ch)]; }

std::vector<MeterBar> MeterState::bars(int ch, bool stereo) const {
    if (ch < 0 || ch >= channels) return {};
    const MeterBar l{disp[size_t(ch * 2)], hold[size_t(ch * 2)]};
    const MeterBar r{disp[size_t(ch * 2 + 1)], hold[size_t(ch * 2 + 1)]};
    if (stereo) return {l, r};
    return {MeterBar{std::max(l.level, r.level), std::max(l.hold, r.hold)}};
}

float MeterState::duckDb(int ch) const {
    if (ch < 0 || ch >= channels) return 0;
    const float g = 1 - duck[size_t(ch)];
    return g < 0.999f ? 20 * std::log10(std::max(1e-4f, g)) : 0;
}

}  // namespace jambo
