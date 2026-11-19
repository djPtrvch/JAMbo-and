// Значения, которые пишет один поток и читает другой (UI ↔ аудио), без блокировок (порт Core/Util/AtomicFloat.swift,
// ветка swift JAMbo).
#pragma once
#include <atomic>

namespace jambo {

class AtomicFloat {
public:
    explicit AtomicFloat(float v = 0) : value(v) {}
    float load() const { return value.load(std::memory_order_relaxed); }
    void store(float v) { value.store(v, std::memory_order_relaxed); }

private:
    std::atomic<float> value;
};

class AtomicCounter {
public:
    long load() const { return value.load(std::memory_order_relaxed); }
    void store(long v) { value.store(v, std::memory_order_relaxed); }

private:
    std::atomic<long> value{0};
};

}  // namespace jambo
