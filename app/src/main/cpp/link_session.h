// Ableton Link: темп, участники, фаза (quantum — 1 такт = 4 доли).
#pragma once
#include <chrono>
#include <atomic>
#include <memory>

namespace ableton { class Link; }

class LinkSession {
public:
    static constexpr double kQuantum = 4.0;
    LinkSession();
    ~LinkSession();

    void enable(bool on);
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void setTempo(double bpm);
    // [включён, участники, темп, доля, фаза в такте] на текущий момент + задержка выхода.
    void info(double* out, int64_t outLatencyUs);


private:
    std::unique_ptr<ableton::Link> link_;
    std::atomic<bool> enabled_{false};
};
