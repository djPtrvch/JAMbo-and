// Источники GRANULAR для тестов (GranularEngineTests.demo и .pad, ветка swift JAMbo): демо-сэмпл прототипа (6 с, моно)
// и пэд A3 (8 с, стерео, период 1/110 с); 44,1 → 48 кГц линейно.
#pragma once
#include "sampler/SampleData.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace grain_sources {

inline std::vector<float> to48(const std::vector<float>& d) {
    const double sr = 44100.0;
    const int n48 = int(double(d.size()) * 48000 / sr);
    std::vector<float> o(static_cast<size_t>(n48));
    for (int i = 0; i < n48; ++i) {
        const double x = double(i) * sr / 48000;
        const int k = int(x);
        const float f = float(x - double(k));
        const float a = d[size_t(k)], b = size_t(k + 1) < d.size() ? d[size_t(k + 1)] : 0;
        o[size_t(i)] = a + (b - a) * f;
    }
    return o;
}

inline jambo::SampleRef demo() {
    static jambo::SampleRef cached = [] {
        const double sr = 44100.0;
        const int len = int(sr) * 6;
        std::vector<float> d(static_cast<size_t>(len), 0.f);
        int64_t r = 12345;
        auto noise = [&] {
            r = (r * 1103515245 + 12345) & 0x7FFFFFFF;
            return double(r) / 1073741823.5 - 1;
        };
        const double notes[8] = {220, 261.63, 329.63, 440, 392, 329.63, 293.66, 261.63};
        const int nl = int(sr * 0.4);
        for (int ni = 0; ni < 8; ++ni) {
            const double f = notes[ni];
            const int s0 = ni * nl, total = int(double(nl) * 1.6);
            for (int i = 0; i < total && s0 + i < len; ++i) {
                const double t = double(i) / sr;
                double v = 0;
                for (int k = 1; k <= 8; ++k)
                    v += std::sin(2 * M_PI * f * double(k) * t) / double(k) * std::exp(-t * (2.5 + double(k) * 1.4));
                if (i < 400) v += noise() * 0.4 * (1 - double(i) / 400);
                d[size_t(s0 + i)] += float(v * 0.5);
            }
        }
        const int p0 = int(sr * 3.4), plen = len - p0;
        const double chord[4] = {110, 164.81, 220, 329.63};
        for (int i = 0; i < plen; ++i) {
            const double t = double(i) / sr, x = double(i) / double(plen);
            const double env = std::pow(std::sin(M_PI * x), 1.5), vib = 1 + 0.004 * std::sin(2 * M_PI * 5 * t);
            double v = 0;
            for (double c : chord)
                for (int k = 1; k <= 5; ++k) v += std::sin(2 * M_PI * c * vib * double(k) * t) / double(k * k);
            d[size_t(p0 + i)] += float(v * 0.12 * env + noise() * 0.03 * env);
        }
        float pk = 0;
        for (float v : d) pk = std::max(pk, std::fabs(v));
        const float g = 0.85f / pk;
        for (auto& v : d) v *= g;
        return std::make_shared<jambo::SampleData>(to48(d), nullptr);
    }();
    return cached;
}

inline jambo::SampleRef makePad(int variant) {
    const double sr = 44100.0;
    const int len = int(sr) * 8, n = 2048;
    std::vector<double> tab(size_t(n + 1), 0.0);
    for (int k = 1; k <= 18; ++k) {
        const double a = 1 / std::pow(double(k), 1.15);
        for (int i = 0; i <= n; ++i) tab[size_t(i)] += a * std::sin(2 * M_PI * double(k) * double(i) / double(n));
    }
    double tm = 0;
    for (double v : tab) tm = std::max(tm, std::fabs(v));
    for (auto& v : tab) v /= tm;
    const double voices[3][2] = {{110, 0.5}, {220, 1}, {440, 0.4}};
    const double det[3] = {-12, 0, 12};
    const int cnt = 3 * 3 * 2;
    int64_t r = 777 + int64_t(variant) * 7919;
    auto uni = [&] {
        r = (r * 1103515245 + 12345) & 0x7FFFFFFF;
        return double(r) / 2147483648.0;
    };
    std::vector<double> ph(static_cast<size_t>(cnt)), dp(static_cast<size_t>(cnt)), inc(static_cast<size_t>(cnt), 0.0);
    for (auto& v : ph) v = uni() * double(n);
    for (auto& v : dp) v = uni() * 6.283;
    std::vector<float> out[2] = {std::vector<float>(size_t(len), 0.f), std::vector<float>(size_t(len), 0.f)};
    double y[2][2] = {{0, 0}, {0, 0}};
    for (int s0 = 0; s0 < len; s0 += 128) {
        const double t = double(s0) / sr, fc = 1000 * std::pow(5.0, t / 8);
        for (int v = 0; v < 3; ++v)
            for (int d = 0; d < 3; ++d)
                for (int ch = 0; ch < 2; ++ch) {
                    const int idx = (v * 3 + d) * 2 + ch;
                    const double cents = det[d] * (ch == 1 ? -1 : 1) + 6 * std::sin(6.283 * 0.09 * t + dp[size_t(idx)]);
                    inc[size_t(idx)] = voices[v][0] * std::pow(2.0, cents / 1200) * double(n) / sr;
                }
        const double a = 1 - std::exp(-6.283 * fc / sr);
        for (int q = 0; q < 128 && s0 + q < len; ++q) {
            double l = 0, rr = 0;
            for (int v = 0; v < 3; ++v)
                for (int d = 0; d < 3; ++d)
                    for (int ch = 0; ch < 2; ++ch) {
                        const int idx = (v * 3 + d) * 2 + ch;
                        double p = ph[size_t(idx)] + inc[size_t(idx)];
                        if (p >= double(n)) p -= double(n);
                        ph[size_t(idx)] = p;
                        const int i0 = int(p);
                        const double val =
                            (tab[size_t(i0)] + (tab[size_t(i0 + 1)] - tab[size_t(i0)]) * (p - double(i0))) * voices[v][1];
                        if (ch == 1) rr += val;
                        else l += val;
                    }
            y[0][0] += a * (l - y[0][0]);
            y[0][1] += a * (y[0][0] - y[0][1]);
            out[0][size_t(s0 + q)] = float(y[0][1]);
            y[1][0] += a * (rr - y[1][0]);
            y[1][1] += a * (y[1][0] - y[1][1]);
            out[1][size_t(s0 + q)] = float(y[1][1]);
        }
    }
    float pk = 0;
    for (auto& o : out)
        for (float v : o) pk = std::max(pk, std::fabs(v));
    const float g = 0.8f / pk;
    for (auto& o : out)
        for (auto& v : o) v *= g;
    const auto right = to48(out[1]);
    auto s = std::make_shared<jambo::SampleData>(to48(out[0]), &right);
    s->period = 1.0 / 110;
    return s;
}

inline jambo::SampleRef pad() {
    static jambo::SampleRef cached = makePad(0);
    return cached;
}

}  // namespace grain_sources
