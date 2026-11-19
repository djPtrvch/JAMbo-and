// LADDER (порт Core/Synth/LadderFilter.swift, ветка swift JAMbo): лестничный фильтр 24 dB в духе Moog — только у SUB.
// Четыре звена TPT, насыщение tanh на входе вместе с обратной связью; GRIT — перегруз входа (0 → +24 dB).
#pragma once
#include <vector>

namespace jambo {

class LadderFilter {
public:
    explicit LadderFilter(int slots) : st(size_t(slots) * 4, 0.0), slots(slots) {}
    int slotCount() const { return slots; }

    void reset(int v) {
        const size_t b = size_t(v) * 4;
        st[b] = st[b + 1] = st[b + 2] = st[b + 3] = 0;
    }

    /// Доля звена TPT для частоты hz.
    static double gain(double hz, double sr);

    /// Кусок голоса v на месте: buf ← сухой + (лестница − сухой)·доля; доля fq → fq + dfq·m, частота — от hz0 к hz1
    /// по отсчётам; qDb — Q голоса (0,7…18,7 dB), grit — 0…1.
    void run(int v, float* buf, int m, double hz0, double hz1, double qDb, double grit, double sr, float fq, float dfq);

private:
    std::vector<double> st;
    int slots;
};

}  // namespace jambo
