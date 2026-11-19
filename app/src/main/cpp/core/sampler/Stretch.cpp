// Таймстретч BEAT (порт Core/Sampler/Stretch.swift).
#include "Stretch.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>

namespace jambo {

Stretcher::Stretcher(int n)
    : outL(size_t(maxChunk), 0.f), outR(size_t(maxChunk), 0.f), window(size_t(grain)),
      gPos(size_t(n * 2), 0.0), gAge(size_t(n * 2), grain), gFirst(size_t(n * 2), 0), gDir(size_t(n * 2), 1.0),
      phase(size_t(n), 0), lastDir(size_t(n), 1.0), target(size_t(corrPoints), 0.f), offsets(size_t(corrPoints), 0.0),
      pIdx(size_t(corrPoints), 0), pRel(size_t(corrPoints), 0.f), pVal(size_t(corrPoints), 0.f), cub(maxChunk),
      tmp(size_t(maxChunk), 0.f) {
    for (int i = 0; i < grain; ++i) window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(grain)));
}

void Stretcher::start(int v, double dir) {
    gAge[size_t(v * 2)] = grain;
    gAge[size_t(v * 2 + 1)] = grain;
    phase[size_t(v)] = 0;
    lastDir[size_t(v)] = dir < 0 ? -1 : 1;
}

float Stretcher::cubic(const float* p, int n, double x) {
    if (x < 0) return 0;
    const int i = int(x);
    const float f = float(x - double(i));
    const float ym1 = i >= 1 && i - 1 < n ? p[i - 1] : 0;
    const float y0 = i < n ? p[i] : 0;
    const float y1 = i + 1 < n ? p[i + 1] : 0;
    const float y2 = i + 2 < n ? p[i + 2] : 0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2 * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

double Stretcher::place(const float* l, int frames, double base, double step, int prev, double lo, double hi) {
    const double b0 = std::min(hi, std::max(lo, base));
    if (prev < 0) return b0;
    const double pp = gPos[size_t(prev)];
    const double ps = gDir[size_t(prev)] * std::abs(step);
    const int pts = corrPoints;
    const double cs = double(corrStep);
    float* tgt = target.data();
    double* off = offsets.data();
    for (int k = 0; k < pts; ++k) {
        const double x = pp + double(k) * cs * ps;
        const int i = x < 0 ? frames : int(x);
        tgt[k] = i < frames ? l[i] : 0;
        off[k] = double(k) * cs * step;
    }
    int iMin = INT_MAX, iMax = INT_MIN;
    bool allPos = true;
    for (int k = 0; k < pts; ++k) {
        const double x = b0 + off[k];
        if (x < 0) allPos = false;
        const int i = x < 0 ? 0 : int(x);
        pIdx[size_t(k)] = i;
        if (i < iMin) iMin = i;
        if (i > iMax) iMax = i;
    }
    for (int k = 0; k < pts; ++k) pRel[size_t(k)] = float(pIdx[size_t(k)] - iMin);
    const double n = double(frames);
    double best = b0;
    float bestScore = -FLT_MAX;
    for (int d = -search; d <= search; d += searchStep) {
        const double c = b0 + double(d);
        if (!(c >= lo && c <= hi)) continue;
        float xy = 0;
        float yy = 1e-9f;
        if (allPos && iMin + d >= 0 && iMax + d < frames) {
            pvGather(l + (iMin + d), pRel.data(), pVal.data(), pts);
            xy = pvDot(tgt, pVal.data(), pts);
            yy += pvSumSq(pVal.data(), pts);
        } else {
            for (int k = 0; k < pts; ++k) {
                const double x = c + off[k];
                if (x >= 0 && x < n) {
                    const float y = l[int(x)];
                    xy += tgt[k] * y;
                    yy += y * y;
                }
            }
        }
        const float sc = xy / std::sqrt(yy);
        if (sc > bestScore) {
            bestScore = sc;
            best = c;
        }
    }
    return best;
}

void Stretcher::fill(int v, const float* l, const float* r, int frames, bool stereo, double pos0, double speed,
                     double ratio, int m, int skip, double lo, double hi) {
    const int g = grain;
    const int h = hop;
    const float* w = window.data();
    const int a0 = v * 2;
    if (speed > 1e-9)
        lastDir[size_t(v)] = 1;
    else if (speed < -1e-9)
        lastDir[size_t(v)] = -1;
    const double dir = lastDir[size_t(v)];
    float* oL = outL.data();
    float* oR = outR.data();
    int j = 0;
    while (j < skip && j < m) {
        oL[j] = 0;
        oR[j] = 0;
        ++j;
    }
    double t = pos0;
    while (j < m) {
        if (phase[size_t(v)] <= 0) {
            // Новое зерно — в ячейку отзвучавшего (старшего) зерна.
            const bool k0 = gAge[size_t(a0)] >= gAge[size_t(a0 + 1)];
            const int k = k0 ? a0 : a0 + 1;
            const int other = k0 ? a0 + 1 : a0;
            const int prev = gAge[size_t(other)] < g ? other : -1;
            gPos[size_t(k)] = place(l, frames, t, dir * ratio, prev, lo, hi);
            gDir[size_t(k)] = dir;
            gAge[size_t(k)] = 0;
            gFirst[size_t(k)] = prev < 0 ? 1 : 0;
            phase[size_t(v)] = h;
        }
        const int len = std::min(m - j, phase[size_t(v)]);
        std::fill(oL + j, oL + j + len, 0.f);
        if (stereo) std::fill(oR + j, oR + j + len, 0.f);
        for (int k = a0; k < a0 + 2; ++k) {
            int o = 0;
            while (o < len && gAge[size_t(k)] < g) {
                const int age = gAge[size_t(k)];
                const bool flat = gFirst[size_t(k)] != 0 && age < h;
                int n = std::min(len - o, g - age);
                if (flat) n = std::min(n, h - age);
                const double x0 = gPos[size_t(k)];
                const double st = gDir[size_t(k)] * ratio;
                grainAdd(l, frames, x0, st, n, oL + j + o, flat ? nullptr : w + age);
                if (stereo) grainAdd(r, frames, x0, st, n, oR + j + o, flat ? nullptr : w + age);
                gPos[size_t(k)] = x0 + st * double(n);
                gAge[size_t(k)] = age + n;
                o += n;
            }
        }
        if (!stereo) std::copy(oL + j, oL + j + len, oR + j);
        phase[size_t(v)] -= len;
        t += speed * double(len);
        j += len;
    }
}

void Stretcher::grainAdd(const float* p, int frames, double x0, double step, int n, float* out, const float* w) {
    float* tp = tmp.data();
    if (!cub.read(p, frames, x0, step, n, 0, double(frames), tp)) {
        for (int i = 0; i < n; ++i) tp[i] = cubic(p, frames, x0 + double(i) * step);
    }
    if (w)
        pvMa(tp, w, out, out, n);
    else
        pvAdd(tp, out, out, n);
}

}  // namespace jambo
