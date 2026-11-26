// MIDI Clock (ClockFollower.swift и ClockPlan.swift): 24 тика на четверть, 6 на шаг.
#pragma once
#include <array>
#include <optional>
#include <vector>

namespace jambo {

class ClockFollower {
public:
    static constexpr double timeout = 0.5;
    bool tick(double at);
    void arm() { armed_ = true; }
    void disarm() { armed_ = false; }
    void reset() { *this = ClockFollower{}; }
    double last() const { return last_; }
    bool anchored() const { return anchored_; }
    std::optional<double> tempo() const;
    void anchor(double at);
    double nextTick(double at) const;
    std::optional<double> stepTime(long step) const;

private:
    void add(double at);
    static constexpr int fitTicks = 48;
    std::array<double, fitTicks> ticks_{}, times_{};
    long count_ = 0, origin_ = 0;
    int samples_ = 0, debt_ = 0, rejected_ = 0;
    double last_ = 0, interval_ = 0, fitTime_ = 0, fitTick_ = 0;
    bool armed_ = false, anchored_ = false;
};

class ClockPlan {
public:
    static constexpr int ticksPerStep = 6;
    struct Grid { long step; double time, stepDuration; };
    struct Event { double time; int status; };
    struct Batch { bool flush = false; std::vector<Event> events; };

    void reset() { *this = ClockPlan{}; }
    Batch update(double now, double bpm, std::optional<Grid> grid, bool startStop, double ahead);

private:
    std::optional<double> last_;
    long index_ = 0;
    bool playing_ = false;
};

}  // namespace jambo
