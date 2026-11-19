// Варпинг в голосе (порт части Core/Sampler/SoundWarp.swift): голос в TSTR ведёт указатель в «отсчётах темпа файла»
// (u), отсчёт сэмпла — по карте меток (между метками — линейно, за крайними — наклон 1). Метки в снимке —
// SoundSnapshot::putWarp.
#include <algorithm>
#include "Sampler.h"

namespace jambo {

double Sampler::warpPos(int v, double x) const {
    const int n = warpN[size_t(v)];
    if (n == 0) return x;
    const double* us = warpU.data() + v * warpCap;
    const double* ps = warpP.data() + v * warpCap;
    if (x <= us[0]) return ps[0] - (us[0] - x);
    if (x >= us[n - 1]) return ps[n - 1] + (x - us[n - 1]);
    int k = 1;
    while (us[k] < x) ++k;
    const double f = (x - us[k - 1]) / std::max(1e-9, us[k] - us[k - 1]);
    return ps[k - 1] + f * (ps[k] - ps[k - 1]);
}

double Sampler::warpInv(int v, double p) const {
    const int n = warpN[size_t(v)];
    if (n == 0) return p;
    const double* us = warpU.data() + v * warpCap;
    const double* ps = warpP.data() + v * warpCap;
    if (p <= ps[0]) return us[0] - (ps[0] - p);
    if (p >= ps[n - 1]) return us[n - 1] + (p - ps[n - 1]);
    int k = 1;
    while (ps[k] < p) ++k;
    const double f = (p - ps[k - 1]) / std::max(1e-9, ps[k] - ps[k - 1]);
    return us[k - 1] + f * (us[k] - us[k - 1]);
}

void Sampler::setupWarp(int v, const SoundData& d, int e, bool ts) {
    int n = 0;
    if (ts && size_t(e) < d.eWarpCount.size()) {
        const int a = int(d.eWarpStart[size_t(e)]);
        const int c = std::min(int(d.eWarpCount[size_t(e)]), warpCap);
        const size_t b = size_t(v * warpCap);
        for (; n < c; ++n) {
            warpU[b + size_t(n)] = d.warpU[size_t(a + n)];
            warpP[b + size_t(n)] = d.warpP[size_t(a + n)];
        }
    }
    warpN[size_t(v)] = n;
    uPos[size_t(v)] = warpInv(v, pos[size_t(v)]);
}

double Sampler::warpChunk(int v, double rt, int m) {
    const double u0 = uPos[size_t(v)];
    const double u1 = u0 + rt * double(m);
    const double p0 = warpPos(v, u0);
    const double p1 = warpPos(v, u1);
    pos[size_t(v)] = p0;
    uPos[size_t(v)] = u1;
    return (p1 - p0) / double(m);
}

void Sampler::warpPreview(int t, uint64_t hash, int64_t at) {
    stopPreview(at);
    one[0] = int16_t(Params::sampleRoot);
    warpPv = true;
    triggerNotes(t, at, one, 1, 0, nullptr, hash, forever, true, warpLive, false);
    warpPv = false;
}

}  // namespace jambo
