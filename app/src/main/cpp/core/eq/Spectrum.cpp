// Спектры EQ (порт Core/EQ/Spectrum.swift).
#include "Spectrum.h"
#include <cmath>

namespace jambo {

RealFFT::RealFFT(int size) : half(size / 2) {
    int b = 0;
    while ((1 << b) < half) ++b;
    bits = b;
    cr.assign(size_t(half), 0);
    ci.assign(size_t(half), 0);
    for (int k = 0; k < half / 2; ++k) {
        const double a = -2 * M_PI * double(k) / double(half);
        wc.push_back(float(std::cos(a)));
        ws.push_back(float(std::sin(a)));
    }
    for (int k = 0; k < half; ++k) {
        const double a = -2 * M_PI * double(k) / double(size);
        sc.push_back(float(std::cos(a)));
        ss.push_back(float(std::sin(a)));
    }
    for (int i = 0; i < half; ++i) {
        int r = 0, x = i;
        for (int j = 0; j < b; ++j) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        rev.push_back(r);
    }
}

void RealFFT::forward(const float* x, float* re, float* im) {
    const int h = half;
    for (int i = 0; i < h; ++i) {
        const size_t j = size_t(rev[size_t(i)]);
        cr[j] = x[2 * i];
        ci[j] = x[2 * i + 1];
    }
    for (int len = 2; len <= h; len <<= 1) {
        const int hl = len / 2;
        const int step = h / len;
        for (int s = 0; s < h; s += len) {
            for (int k = 0; k < hl; ++k) {
                const float c = wc[size_t(k * step)], sn = ws[size_t(k * step)];
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
    for (int k = 0; k < h; ++k) {
        const size_t K = size_t(k);
        const size_t m = size_t(k == 0 ? 0 : h - k);
        const float zr = cr[K], zi = ci[K], yr = cr[m], yi = -ci[m];
        const float er = 0.5f * (zr + yr), ei = 0.5f * (zi + yi);
        const float dr = zr - yr, di = zi - yi;
        const float orr = 0.5f * di, oi = -0.5f * dr;
        re[k] = er + sc[K] * orr - ss[K] * oi;
        im[k] = ei + sc[K] * oi + ss[K] * orr;
    }
}

SpectrumAnalyzer::SpectrumAnalyzer()
    : db(bins, -240.f), window(size), input(size, 0.f), mag(bins, 0.f), re(bins, 0.f), im(bins, 0.f), fft(size) {
    for (int i = 0; i < size; ++i) {
        const double x = 2 * M_PI * double(i) / double(size);
        window[size_t(i)] = float(0.42 - 0.5 * std::cos(x) + 0.08 * std::cos(2 * x));
    }
}

void SpectrumAnalyzer::analyze(const SpectrumTap& tap) {
    tap.read(input.data(), size);
    analyzeInput();
}

void SpectrumAnalyzer::analyze(const std::vector<float>& x) {
    for (size_t i = 0; i < size_t(size); ++i) input[i] = i < x.size() ? x[i] : 0;
    analyzeInput();
}

void SpectrumAnalyzer::analyzeInput() {
    for (size_t i = 0; i < size_t(size); ++i) input[i] *= window[i];
    fft.forward(input.data(), re.data(), im.data());
    const float scale = 1 / float(size);
    const float k = smoothing;
    for (size_t i = 0; i < size_t(bins); ++i) {
        const float a = std::sqrt(re[i] * re[i] + im[i] * im[i]) * scale;
        float m = k * mag[i] + (1 - k) * a;
        if (!std::isfinite(m)) m = 0;
        mag[i] = m;
        db[i] = m > 0 ? 20 * std::log10(m) : -240;
    }
}

}  // namespace jambo
