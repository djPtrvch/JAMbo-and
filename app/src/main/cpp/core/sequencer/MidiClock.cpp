#include "MidiClock.h"
#include <algorithm>
#include <cmath>

namespace jambo {

bool ClockFollower::tick(double at) {
    if (at < last_) return false;
    const double gap = at - last_;
    bool fits = true;
    if (last_ > 0 && interval_ > 0) {
        if (gap > std::max(interval_ * 2.5, 0.06)) {
            debt_ += std::max(0, int(std::round(gap / interval_)) - 1);
            fits = false;
        } else if (gap < interval_ * 0.5) {
            debt_ = std::max(0, debt_ - 1);
            fits = false;
        } else if (gap > interval_ * 1.5) {
            fits = false;
        } else if (debt_ > 0) {
            count_ += debt_;
            debt_ = 0;
        }
    }
    last_ = at;
    ++count_;
    if (fits) {
        rejected_ = 0;
        add(at);
    } else if (++rejected_ >= 6) {
        samples_ = debt_ = rejected_ = 0;
        add(at);
    }
    if (!armed_) return false;
    armed_ = false;
    origin_ = count_ - 1;
    anchored_ = true;
    return true;
}

void ClockFollower::add(double at) {
    const int slot = samples_++ % fitTicks;
    ticks_[size_t(slot)] = double(count_ - 1);
    times_[size_t(slot)] = at;
    const int size = std::min(samples_, fitTicks);
    fitTick_ = fitTime_ = 0;
    for (int i = 0; i < size; ++i) {
        fitTick_ += ticks_[size_t(i)];
        fitTime_ += times_[size_t(i)];
    }
    fitTick_ /= size;
    fitTime_ /= size;
    double xx = 0, xy = 0;
    for (int i = 0; i < size; ++i) {
        const double dx = ticks_[size_t(i)] - fitTick_;
        xx += dx * dx;
        xy += dx * (times_[size_t(i)] - fitTime_);
    }
    interval_ = xx > 0 ? xy / xx : 0;
}

std::optional<double> ClockFollower::tempo() const {
    if (samples_ < 4 || interval_ < 0.002 || interval_ > 0.25) return std::nullopt;
    return 60 / (interval_ * 24);
}

void ClockFollower::anchor(double at) {
    if (interval_ <= 0) return;
    origin_ = std::lround(fitTick_ + (at - fitTime_) / interval_);
    anchored_ = true;
}

double ClockFollower::nextTick(double at) const {
    if (!tempo()) return at;
    return fitTime_ + (std::ceil(fitTick_ + (at - fitTime_) / interval_) - fitTick_) * interval_;
}

std::optional<double> ClockFollower::stepTime(long step) const {
    if (!tempo() || !anchored_) return std::nullopt;
    return fitTime_ + (double(origin_ + step * ClockPlan::ticksPerStep) - fitTick_) * interval_;
}

ClockPlan::Batch ClockPlan::update(double now, double bpm, std::optional<Grid> grid, bool startStop, double ahead) {
    Batch out;
    const double until = now + ahead;
    const double period = 60 / (bpm * 24);
    if (grid && grid->stepDuration > 0) {
        const auto& g = *grid;
        const double duration = g.stepDuration / ticksPerStep;
        if (!playing_) {
            playing_ = true;
            out.flush = true;
            const double start = g.time - g.step * g.stepDuration;
            if (last_) {
                for (double t = *last_; t > now; t -= period)
                    if (t < start - period / 2) out.events.push_back({t, 0xF8});
                std::reverse(out.events.begin(), out.events.end());
            }
            if (startStop) out.events.push_back({start, 0xFA});
            out.events.push_back({start, 0xF8});
            index_ = 0;
            last_ = start;
        }
        const long base = g.step * ticksPerStep;
        const long first = base + long(std::ceil((now - duration - g.time) / duration));
        index_ = std::max(index_, first - 1);
        while (true) {
            const double time = g.time + (index_ + 1 - base) * duration;
            if (time > until) break;
            ++index_;
            if (time < now - duration || (last_ && time <= *last_)) continue;
            out.events.push_back({time, 0xF8});
            last_ = time;
        }
        return out;
    }
    if (playing_) {
        playing_ = false;
        if (startStop) out.events.push_back({now, 0xFC});
    }
    double next = last_.value_or(now - period) + period;
    if (next < now - period) next = now;
    while (next <= until) {
        out.events.push_back({next, 0xF8});
        last_ = next;
        next += period;
    }
    return out;
}

}  // namespace jambo
