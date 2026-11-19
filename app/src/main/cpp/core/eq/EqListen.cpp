// Прослушивание полосы EQ (порт Core/EQ/EqListen.swift).
#include "EqListen.h"
#include <algorithm>
#include <cmath>
#include "EqCoeffs.h"
#include "EqUnit.h"

namespace jambo {

EqListen::EqListen(double sampleRate)
    : sr(sampleRate), st(maxLanes * 8, 0.0), tmpL(EqDsp::maxFrames, 0.f), tmpR(EqDsp::maxFrames, 0.f) {
    channel.store(-1);
}

EqListen::Filter EqListen::filter(const EqBand& b) {
    switch (b.type) {
    case EqSpec::lowCut:
    case EqSpec::lowShelf:
        return {lowpass, butterQ};
    case EqSpec::highCut:
    case EqSpec::highShelf:
        return {highpass, butterQ};
    default:
        return {bandpass, std::max(0.1, b.q)};
    }
}

void EqListen::coeffs(int kind, double hz, double q, double sr, double* out) {
    const int t = kind == lowpass ? EqSpec::highCut : (kind == highpass ? EqSpec::lowCut : EqCoeffs::bandpass);
    EqCoeffs::coeffs(t, hz, 0, q, sr, out);
}

EqListen::Span EqListen::span(const EqBand& b) {
    const Filter f = filter(b);
    if (f.kind == lowpass) return {0, b.freq};
    if (f.kind == highpass) return {b.freq, std::numeric_limits<double>::infinity()};
    const double h = 1 / (2 * f.q);
    const double r = std::sqrt(1 + h * h);
    return {b.freq * (r - h), b.freq * (r + h)};
}

void EqListen::process(int ch, EqUnit& u, int frames) {
    const int target = int(channel.load());
    if (active != ch) {
        if (active >= 0 || target != ch) return;
        active = ch;
        mix = 0;
        curKind = -1;
        std::fill(st.begin(), st.end(), 0.0);
    }
    const int n = std::min(frames, EqDsp::maxFrames);
    if (n <= 0) return;
    const bool on = target == ch;
    if (on)
        prepare(n);
    else
        hold();
    const int lanes = std::min(u.lanes, maxLanes);
    const float m0 = mix;
    for (int lane = 0; lane < lanes; ++lane) {
        float* l = u.laneL[size_t(lane)];
        float* r = u.laneR[size_t(lane)];
        if (l && r) run(lane, l, r, n, m0, on);
    }
    const float step = float(n) / fadeFrames;
    mix = on ? std::min(1.f, m0 + step) : std::max(0.f, m0 - step);
    for (int t = 0; t < 5; ++t) co[t] = next[t];
    if (!on && mix <= 0) active = -1;
}

void EqListen::prepare(int n) {
    const int k = int(kind.load());
    const double f = std::max(10.0, std::min(sr * 0.49, double(freq.load())));
    const double qq = double(q.load());
    const int msNew = int(ms.load());
    if (k != curKind || msNew != curMs) {
        curKind = k;
        curMs = msNew;
        curFreq = f;
        curQ = qq;
        std::fill(st.begin(), st.end(), 0.0);
        coeffs(k, f, qq, sr, co);
    } else {
        const double a = 1 - std::exp(-double(n) / (tau * sr));
        curFreq = std::exp(std::log(curFreq) + (std::log(f) - std::log(curFreq)) * a);
        curQ = curQ + (qq - curQ) * a;
    }
    coeffs(curKind, curFreq, curQ, sr, next);
    const double inv = 1 / double(n);
    for (int t = 0; t < 5; ++t) dc[t] = (next[t] - co[t]) * inv;
}

void EqListen::hold() {
    for (int t = 0; t < 5; ++t) {
        dc[t] = 0;
        next[t] = co[t];
    }
}

void EqListen::run(int lane, float* l, float* r, int n, float m0, bool on) {
    double* z = st.data() + lane * 8;
    float* tl = tmpL.data();
    float* tr = tmpR.data();
    if (curMs == 0) {
        std::copy(l, l + n, tl);
        std::copy(r, r + n, tr);
        EqDsp::biquadRamp(tl, n, co, dc, z);
        EqDsp::biquadRamp(tr, n, co, dc, z + 4);
    } else {
        const bool side = curMs == 2;
        for (int i = 0; i < n; ++i) {
            const float hl = l[i] * 0.5f;
            const float hr = r[i] * 0.5f;
            tl[i] = side ? hl - hr : hl + hr;
        }
        EqDsp::biquadRamp(tl, n, co, dc, z);
        for (int i = 0; i < n; ++i) tr[i] = side ? -tl[i] : tl[i];
    }
    const float step = (on ? 1.f : -1.f) / fadeFrames;
    float m = m0;
    for (int i = 0; i < n; ++i) {
        m = std::max(0.f, std::min(1.f, m + step));
        l[i] += (tl[i] - l[i]) * m;
        r[i] += (tr[i] - r[i]) * m;
    }
}

}  // namespace jambo
