#include "MixStereo.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace jambo {

/// Баттерворт 2-го порядка (RBJ, Q 0,7071).
static void coeffs(double f, bool low, double out[5]) {
    const double w = 2 * M_PI * f / 48000;
    const double alpha = std::sin(w) / (2 * 0.70710678);
    const double c = std::cos(w);
    const double a0 = 1 + alpha;
    const double b1 = low ? 1 - c : -(1 + c);
    const double b0 = low ? (1 - c) / 2 : (1 + c) / 2;
    out[0] = b0 / a0;
    out[1] = b1 / a0;
    out[2] = b0 / a0;
    out[3] = -2 * c / a0;
    out[4] = (1 - alpha) / a0;
}

BandStereo::BandStereo() {
    double lp150[5], hp150[5], lp2k[5], hp2k[5];
    coeffs(150, true, lp150);
    coeffs(150, false, hp150);
    coeffs(2000, true, lp2k);
    coeffs(2000, false, hp2k);
    const double* sec[3][2] = {{lp150, lp150}, {hp150, lp2k}, {hp2k, hp2k}};
    for (int b = 0; b < 3; b++)
        for (int c = 0; c < 2; c++)
            for (int k = 0; k < 2; k++) {
                const double* q = sec[b][k];
                f[size_t(b * 2 + c)][size_t(k)] = Biquad{q[0], q[1], q[2], q[3], q[4]};
            }
}

void BandStereo::tick(const MasterScope& s) {
    const long end = s.gonioL.written.load();
    const int n = seen < 0 ? 0 : int(std::min<long>(cap, end - seen));
    seen = end;
    if (n <= 0) return;
    s.gonioL.read(inL.data(), n, end);
    s.gonioR.read(inR.data(), n, end);
    const float a = float(std::exp(-double(n) / (0.3 * 48000)));
    for (int b = 0; b < 3; b++) {
        auto& fl = f[size_t(b * 2)];
        auto& fr = f[size_t(b * 2 + 1)];
        float sl = 0, sr = 0, slr = 0;
        for (int i = 0; i < n; i++) {
            const float l = fl[1].run(fl[0].run(inL[size_t(i)]));
            const float r = fr[1].run(fr[0].run(inR[size_t(i)]));
            sl += l * l;
            sr += r * r;
            slr += l * r;
        }
        const float k = (1 - a) / float(n);
        ll[size_t(b)] = ll[size_t(b)] * a + sl * k;
        rr[size_t(b)] = rr[size_t(b)] * a + sr * k;
        lr[size_t(b)] = lr[size_t(b)] * a + slr * k;
        measure(b);
    }
}

void BandStereo::measure(int b) {
    const size_t i = size_t(b);
    const float p = ll[i] + rr[i];
    live[i] = p > 2e-7f;
    if (!live[i]) return;
    const float c = lr[i] / std::max(1e-20f, std::sqrt(ll[i] * rr[i]));
    corr[i] = std::max(-1.f, std::min(1.f, c));
    const float mid = std::max(0.f, p + 2 * lr[i]);
    const float side = std::max(0.f, p - 2 * lr[i]);
    width[i] = 200 * side / std::max(1e-20f, mid + side);
}

void BandStereo::reset() {
    for (size_t b = 0; b < 3; b++) {
        ll[b] = rr[b] = lr[b] = 0;
        live[b] = false;
    }
}

LoudHistory::LoudHistory() : values(points, -std::numeric_limits<float>::infinity()) {}

void LoudHistory::tick(const Loudness& l, float framePeak, bool playing, double now) {
    if (!playing) return;
    peak = std::max(peak, framePeak);
    if (now - last < step) return;
    last = now;
    const float st = l.shortTerm.load();
    values[size_t(head)] = st;
    head = (head + 1) % points;
    if (std::isfinite(st) && st > -70) {
        const int b = int(std::floor((double(st) + 70) * 10));
        hist[size_t(std::max(0, std::min(bins - 1, b)))] += 1;
    }
    if (now - lraAt > 0.5) {
        lraAt = now;
        lra = computeLra();
    }
}

void LoudHistory::reset() {
    std::fill(values.begin(), values.end(), -std::numeric_limits<float>::infinity());
    std::fill(hist.begin(), hist.end(), 0);
    head = 0;
    peak = 0;
    lra = -1;
}

double LoudHistory::computeLra() const {
    int n = 0;
    double e = 0;
    for (int b = 0; b < bins; b++)
        if (hist[size_t(b)] > 0) {
            n += hist[size_t(b)];
            e += double(hist[size_t(b)]) * std::pow(10, level(b) / 10);
        }
    if (n < 10) return -1;
    const double gate = 10 * std::log10(e / double(n)) - 20;
    int kept = 0;
    for (int b = 0; b < bins; b++)
        if (level(b) >= gate) kept += hist[size_t(b)];
    if (kept < 10) return -1;
    return std::max(0.0, percentile(gate, kept, 0.95) - percentile(gate, kept, 0.10));
}

double LoudHistory::percentile(double gate, int kept, double q) const {
    const double target = q * double(kept - 1);
    int acc = 0;
    for (int b = 0; b < bins; b++)
        if (level(b) >= gate) {
            acc += hist[size_t(b)];
            if (double(acc) > target) return level(b);
        }
    return level(bins - 1);
}

}  // namespace jambo
