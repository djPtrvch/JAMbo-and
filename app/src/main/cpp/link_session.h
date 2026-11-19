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

    // Для колбэка (realtime-safe): часы Link и доли на отрезке [t0, t1].
    std::chrono::microseconds clockMicros() const;
    // Время Link для кадра потока: регрессия по счётчику кадров (HostTimeFilter),
    // а не часы в момент колбэка — колбэки идут неровно. Только из аудиопотока.
    std::chrono::microseconds hostTimeAtFrame(double frame);
    void resetHostTime();
    void beatsAt(std::chrono::microseconds t0, std::chrono::microseconds t1,
                 double& b0, double& b1, double& phaseAtNextBeat) const;

private:
    std::unique_ptr<ableton::Link> link_;
    struct Filter;
    std::unique_ptr<Filter> filter_;
    std::atomic<bool> enabled_{false};
};
