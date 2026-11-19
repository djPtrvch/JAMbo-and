// Замеры колбэка по разделам (порт Core/Util/RenderProfile.swift, ветка swift JAMbo): метки на уровне блока — время от
// прошлой метки идёт в раздел. Каждую секунду звука — средняя доля блока и наибольшая доля одного блока по разделам и
// всего, и наибольшая доля с последнего сброса — seqlock. Выключенный замер — одна проверка на метку.
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jambo {

class RenderProfile {
public:
    static constexpr int seq = 0, voices = 1, synth = 2, eq = 3, mix = 4, fx = 5, master = 6, meters = 7;
    static constexpr int count = 8;
    static const std::vector<std::string>& names();

    struct Stats {
        std::vector<float> avg, peak, worst;
        int blocks = 0;
    };

    explicit RenderProfile(double sampleRate, double windowSeconds = 1);

    bool enabled() const { return flag.load(std::memory_order_relaxed) != 0; }
    void setEnabled(bool on) { flag.store(on ? 1 : 0, std::memory_order_relaxed); }
    void resetWorst() { resetFlag.store(1, std::memory_order_relaxed); }
    /// Монотонное время, нс.
    static uint64_t now();
    /// Колбэк: начало блока.
    void begin();
    /// Колбэк: время от прошлой метки — в раздел section.
    void lap(int section) {
        if (!active) return;
        const uint64_t t = now();
        block[size_t(section)] += double(t - last);
        last = t;
    }
    /// Колбэк: конец блока из n кадров.
    void end(int n);
    /// Интерфейс: последнее окно (nullopt — ещё не было).
    std::optional<Stats> read() const;

private:
    double sampleRate;
    int window;
    std::atomic<int> flag{0}, resetFlag{0};
    bool active = false;
    uint64_t last = 0;
    std::vector<double> block, sum, peakNow, worst;
    int frames = 0, blocks = 0;
    std::vector<std::atomic<float>> out;
    std::atomic<long> version{0};
    std::atomic<int> blocksOut{0};

    void publish();
    void reset();
};

}  // namespace jambo
