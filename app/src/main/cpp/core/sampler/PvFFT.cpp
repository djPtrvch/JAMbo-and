// БПФ фазового вокодера (порт Core/Sampler/PvFFT.swift, ветка без Accelerate).
#include "PvFFT.h"
#include <cmath>

namespace jambo {

PvFFT::PvFFT(int size) : n(size), half(size / 2) {
    int b = 0;
    while ((1 << b) < half) ++b;
    bits = b;
    cr.assign(size_t(half), 0);
    ci.assign(size_t(half), 0);
    wc.resize(size_t(half / 2));
    ws.resize(size_t(half / 2));
    for (int k = 0; k < half / 2; ++k) {
        const double a = -2 * M_PI * double(k) / double(half);
        wc[size_t(k)] = float(std::cos(a));
        ws[size_t(k)] = float(std::sin(a));
    }
    sc.resize(size_t(half + 1));
    ss.resize(size_t(half + 1));
    for (int k = 0; k <= half; ++k) {
        const double a = -2 * M_PI * double(k) / double(size);
        sc[size_t(k)] = float(std::cos(a));
        ss[size_t(k)] = float(std::sin(a));
    }
    rev.resize(size_t(half));
    for (int i = 0; i < half; ++i) {
        int r = 0, x = i;
        for (int j = 0; j < b; ++j) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        rev[size_t(i)] = r;
    }
    // Масштаб круга — по импульсу.
    std::vector<float> x(size_t(size), 0.f), re(size_t(half + 1), 0.f), im(size_t(half + 1), 0.f);
    x[0] = 1;
    forward(x.data(), re.data(), im.data());
    inverse(re.data(), im.data(), x.data());
    gain_ = x[0] != 0 ? x[0] : 1;
}

void PvFFT::forward(const float* x, float* re, float* im) {
    const int h = half;
    for (int i = 0; i < h; ++i) {
        const int j = rev[size_t(i)];
        cr[size_t(j)] = x[2 * i];
        ci[size_t(j)] = x[2 * i + 1];
    }
    complex(false);
    // X[k] = (Z[k] + Z*[h−k])/2 + e^(−2πik/n)·(Z[k] − Z*[h−k])/(2i).
    for (int k = 0; k <= h; ++k) {
        const size_t a = size_t(k == h ? 0 : k);
        const size_t m = size_t(k == 0 ? 0 : h - k);
        const float zr = cr[a], zi = ci[a], yr = cr[m], yi = -ci[m];
        const float er = 0.5f * (zr + yr), ei = 0.5f * (zi + yi);
        const float dr = zr - yr, di = zi - yi;
        const float orr = 0.5f * di, oi = -0.5f * dr;
        re[k] = er + sc[size_t(k)] * orr - ss[size_t(k)] * oi;
        im[k] = ei + sc[size_t(k)] * oi + ss[size_t(k)] * orr;
    }
    im[0] = 0;
    im[h] = 0;
}

void PvFFT::inverse(const float* re, const float* im, float* x) {
    const int h = half;
    // Z[k] = E + i·O: E = (X[k] + X*[h−k])/2, O = (X[k] − X*[h−k])/2 · e^(+2πik/n); z = ОБПФ(Z).
    for (int k = 0; k < h; ++k) {
        const int m = h - k;
        const float xr = re[k], xi = im[k], yr = re[m], yi = -im[m];
        const float er = 0.5f * (xr + yr), ei = 0.5f * (xi + yi);
        const float dr = 0.5f * (xr - yr), di = 0.5f * (xi - yi);
        const float c = sc[size_t(k)], s = -ss[size_t(k)];
        const float orr = dr * c - di * s, oi = dr * s + di * c;
        const size_t j = size_t(rev[size_t(k)]);
        cr[j] = er - oi;
        ci[j] = ei + orr;
    }
    complex(true);
    for (int i = 0; i < h; ++i) {
        x[2 * i] = cr[size_t(i)];
        x[2 * i + 1] = ci[size_t(i)];
    }
}

void PvFFT::complex(bool inv) {
    const int h = half;
    const float sgn = inv ? -1.f : 1.f;
    for (int len = 2; len <= h; len <<= 1) {
        const int hl = len / 2;
        const int step = h / len;
        for (int s = 0; s < h; s += len) {
            for (int k = 0; k < hl; ++k) {
                const float c = wc[size_t(k * step)], sn = ws[size_t(k * step)] * sgn;
                const size_t a = size_t(s + k), b = a + size_t(hl);
                const float tr = cr[b] * c - ci[b] * sn;
                const float ti = cr[b] * sn + ci[b] * c;
                cr[b] = cr[a] - tr;
                ci[b] = ci[a] - ti;
                cr[a] += tr;
                ci[a] += ti;
            }
        }
    }
}

}  // namespace jambo
