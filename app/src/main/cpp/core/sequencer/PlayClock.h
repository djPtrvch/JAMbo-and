// Ход транспорта из колбэка для интерфейса (порт Core/Sequencer/PlayClock.swift, ветка swift JAMbo): одна запись —
// seqlock: читатель повторяет, пока писатель не закончил (колбэк не ждёт никогда).
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace jambo {

class PlayClock {
public:
    struct State {
        bool playing = false;
        long nextTick = 0;
        double nextFrame = 0;
        double stepFrames = 6000;
        double blockFrame = 0;
        double blockHost = 0;
    };

    /// Колбэк: записать состояние.
    void write(const State& s) {
        const long v = version.load(std::memory_order_relaxed);
        version.store(v + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        playing.store(s.playing ? 1 : 0, std::memory_order_relaxed);
        nextTick.store(s.nextTick, std::memory_order_relaxed);
        nextFrame.store(s.nextFrame, std::memory_order_relaxed);
        stepFrames.store(s.stepFrames, std::memory_order_relaxed);
        blockFrame.store(s.blockFrame, std::memory_order_relaxed);
        blockHost.store(s.blockHost, std::memory_order_relaxed);
        version.store(v + 2, std::memory_order_release);
    }

    /// Интерфейс: прочитать целую запись.
    State read() const {
        while (true) {
            const long v0 = version.load(std::memory_order_acquire);
            if (v0 & 1) continue;
            State s;
            s.playing = playing.load(std::memory_order_relaxed) != 0;
            s.nextTick = nextTick.load(std::memory_order_relaxed);
            s.nextFrame = nextFrame.load(std::memory_order_relaxed);
            s.stepFrames = stepFrames.load(std::memory_order_relaxed);
            s.blockFrame = blockFrame.load(std::memory_order_relaxed);
            s.blockHost = blockHost.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (version.load(std::memory_order_relaxed) == v0) return s;
        }
    }

private:
    std::atomic<long> version{0};
    std::atomic<int> playing{0};
    std::atomic<long> nextTick{0};
    std::atomic<double> nextFrame{0}, stepFrames{0}, blockFrame{0}, blockHost{0};
};

}  // namespace jambo
