// Сигнал дорожек для FREQ и DYN при MIX (порт Core/Engine/TrackTaps.swift, ветка swift JAMbo): кольца по дорожкам, моно
// (L + R)/2; пишет колбэк, читает интерфейс.
#pragma once
#include <vector>
#include "sampler/SoundSnapshot.h"
#include "util/AtomicFloat.h"

namespace jambo {

class TrackTaps {
public:
    static constexpr int capacity = 8192;
    static constexpr int mask = capacity - 1;
    static constexpr int tracks = SoundData::maxTracks;
    TrackTaps() : buf(size_t(capacity) * tracks, 0.f) {}
    /// Всего записано отсчётов (у каждой дорожки); on — 1, съём включён.
    AtomicCounter written, on;

    /// Колбэк: блок n отсчётов выходов дорожек 0…count−1; остальные — тишина.
    void push(const float* outL, const float* outR, int stride, int count, int n) {
        const long w = written.load();
        for (int t = 0; t < tracks; ++t) {
            float* b = buf.data() + size_t(t) * capacity;
            if (t < count) {
                const float* l = outL + t * stride;
                const float* r = outR + t * stride;
                for (int i = 0; i < n; ++i) b[(w + i) & mask] = 0.5f * (l[i] + r[i]);
            } else {
                for (int i = 0; i < n; ++i) b[(w + i) & mask] = 0;
            }
        }
        written.store(w + n);
    }
    /// Интерфейс: последние count отсчётов дорожки t.
    void read(int t, float* out, int count) const {
        const long w = written.load();
        const float* b = buf.data() + size_t(t) * capacity;
        for (int i = 0; i < count; ++i) out[i] = b[(w - count + i) & mask];
    }

private:
    std::vector<float> buf;
};

}  // namespace jambo
