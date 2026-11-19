// Замеры колбэка по разделам (порт Core/Util/RenderProfile.swift).
#include "RenderProfile.h"
#include <chrono>

namespace jambo {

const std::vector<std::string>& RenderProfile::names() {
    static const std::vector<std::string> v = {"Seq", "Samples", "Synth", "EQ", "Mix", "FX", "Master", "Meters"};
    return v;
}

RenderProfile::RenderProfile(double sr, double windowSeconds)
    : sampleRate(sr), window(int(sr * windowSeconds)), block(count + 1, 0), sum(count + 1, 0), peakNow(count + 1, 0),
      worst(count + 1, 0), out(size_t(3 * (count + 1))) {
    for (auto& x : out) x.store(0, std::memory_order_relaxed);
    (void)now();
}

uint64_t RenderProfile::now() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

void RenderProfile::begin() {
    const bool on = flag.load(std::memory_order_relaxed) != 0;
    if (!on) {
        if (active) {
            active = false;
            reset();
        }
        return;
    }
    active = true;
    const bool clear = resetFlag.load(std::memory_order_relaxed) != 0;
    if (clear) resetFlag.store(0, std::memory_order_relaxed);
    for (size_t k = 0; k <= size_t(count); ++k) {
        block[k] = 0;
        if (clear) worst[k] = 0;
    }
    last = now();
}

void RenderProfile::end(int n) {
    if (!active || n <= 0) return;
    const size_t c = count;
    double total = 0;
    for (size_t k = 0; k < c; ++k) total += block[k];
    block[c] = total;
    for (size_t k = 0; k <= c; ++k) sum[k] += block[k];
    // Пик — доля своего блока.
    const double blockNs = double(n) / sampleRate * 1e9;
    for (size_t k = 0; k <= c; ++k) {
        const double share = block[k] / blockNs;
        if (share > peakNow[k]) peakNow[k] = share;
        if (share > worst[k]) worst[k] = share;
    }
    frames += n;
    blocks += 1;
    if (frames >= window) publish();
}

void RenderProfile::publish() {
    const size_t c = count;
    const double wallNs = double(frames) / sampleRate * 1e9;
    const long v = version.load(std::memory_order_relaxed);
    version.store(v + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (size_t k = 0; k <= c; ++k) {
        out[k].store(float(sum[k] / wallNs), std::memory_order_relaxed);
        out[c + 1 + k].store(float(peakNow[k]), std::memory_order_relaxed);
        out[2 * (c + 1) + k].store(float(worst[k]), std::memory_order_relaxed);
    }
    blocksOut.store(blocks, std::memory_order_relaxed);
    version.store(v + 2, std::memory_order_release);
    reset();
}

void RenderProfile::reset() {
    for (size_t k = 0; k <= size_t(count); ++k) {
        sum[k] = 0;
        peakNow[k] = 0;
    }
    frames = 0;
    blocks = 0;
}

std::optional<RenderProfile::Stats> RenderProfile::read() const {
    const size_t c = count;
    while (true) {
        const long v0 = version.load(std::memory_order_acquire);
        if (v0 == 0) return std::nullopt;
        if (v0 & 1) continue;
        Stats s;
        for (size_t k = 0; k <= c; ++k) {
            s.avg.push_back(out[k].load(std::memory_order_relaxed));
            s.peak.push_back(out[c + 1 + k].load(std::memory_order_relaxed));
            s.worst.push_back(out[2 * (c + 1) + k].load(std::memory_order_relaxed));
        }
        s.blocks = blocksOut.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (version.load(std::memory_order_relaxed) == v0) return s;
    }
}

}  // namespace jambo
