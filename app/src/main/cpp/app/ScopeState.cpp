#include "ScopeState.h"
#include <algorithm>
#include <cmath>

namespace jambo {

ScopeState::ScopeState() : buf(SpectrumTap::capacity, 0.f) {}

void ScopeState::tick(const MasterScope& s, const std::array<bool, 4>& on) {
    const int w = std::max(1, width);
    const int spc = std::max(1, int(window * 48000.0) / w);
    for (int k = 0; k < 4; k++) {
        if (int(mins[size_t(k)].size()) != w) {
            mins[size_t(k)].assign(size_t(w), 0.f);
            maxs[size_t(k)].assign(size_t(w), 0.f);
            pos[size_t(k)] = 0;
            carry[size_t(k)] = 0;
        }
        const SpectrumTap& tap = s.taps[k];
        const long end = tap.written.load();
        if (!on[size_t(k)]) {
            if (seen[size_t(k)] >= 0) clear(k);
            seen[size_t(k)] = -1;
            continue;
        }
        const int n = seen[size_t(k)] < 0 ? 0 : int(std::min<long>(SpectrumTap::capacity, end - seen[size_t(k)]));
        seen[size_t(k)] = end;
        if (n <= 0) continue;
        tap.read(buf.data(), n, end);
        fold(k, n, spc, w);
    }
    const long end = s.gonioL.written.load();
    s.gonioL.read(gl.data(), gonioN, end);
    s.gonioR.read(gr.data(), gonioN, end);
    gonioOk = end > 0;
    corr = correlation();
}

std::optional<float> ScopeState::correlation() const {
    float lr = 0, ll = 0, rr = 0;
    for (int i = 0; i < gonioN; i++) {
        const float l = gl[size_t(i)], r = gr[size_t(i)];
        lr += l * r;
        ll += l * l;
        rr += r * r;
    }
    const float floor = 1e-6f * float(gonioN);
    if (ll + rr < floor) return std::nullopt;
    const float v = lr / std::max(1e-12f, std::sqrt(ll * rr));
    const float c = std::max(-1.f, std::min(1.f, v));
    if (!corr) return c;
    return *corr * 0.8f + c * 0.2f;
}

void ScopeState::fold(int k, int n, int spc, int w) {
    float lo = cMin[size_t(k)], hi = cMax[size_t(k)];
    int c = carry[size_t(k)];
    int p = pos[size_t(k)];
    for (int i = 0; i < n; i++) {
        const float v = buf[size_t(i)];
        if (c == 0) {
            lo = v;
            hi = v;
        } else {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        c += 1;
        if (c >= spc) {
            mins[size_t(k)][size_t(p)] = lo;
            maxs[size_t(k)][size_t(p)] = hi;
            p = (p + 1) % w;
            c = 0;
        }
    }
    cMin[size_t(k)] = lo;
    cMax[size_t(k)] = hi;
    carry[size_t(k)] = c;
    pos[size_t(k)] = p;
}

void ScopeState::clear(int k) {
    std::fill(mins[size_t(k)].begin(), mins[size_t(k)].end(), 0.f);
    std::fill(maxs[size_t(k)].begin(), maxs[size_t(k)].end(), 0.f);
}

}  // namespace jambo
