// Порт Core/Sampler/TempoDetect.swift и Onsets.swift (ветка swift JAMbo).
#include "TempoDetect.h"
#include <algorithm>
#include <cmath>
#include "SampleData.h"

namespace jambo {

static constexpr double coreRate = 48000;

namespace TempoDetect {

double loopBpm(const SampleData& s) {
    const double dur = double(s.frames()) / coreRate;
    if (dur < 240.0 / 170) return 0;
    double beats = 4;
    while (60 * beats / dur < 85) beats *= 2;
    const double byLength = 60 * beats / dur;
    if (dur < 6) return byLength;
    const std::optional<double> det = bpm(s);
    if (!det) return byLength;
    for (double m : {1.0, 2.0, 0.5})
        if (std::fabs(*det / (byLength * m) - 1) < 0.02) return byLength * m;
    return *det;
}

std::optional<double> bpm(const SampleData& s) {
    const double rate = coreRate;
    const double fr = rate / double(hop);
    const int want = int(40 * rate);
    const int total = s.frames();
    if (total < int(6 * rate)) return std::nullopt;
    const int len = std::min(want, total);
    const int from = std::min(total - len, total / 5);
    const std::vector<double> onset = envelope(s, from, len);
    const int n = int(onset.size());
    const int maxLag = int(8 * 60 * fr / lo) + 2;
    if (n < maxLag + 16) return std::nullopt;
    const std::vector<double> r = autocorr(onset, maxLag);
    if (r[0] <= 0) return std::nullopt;
    const double span = double(maxLag - 2);
    double best = 0, bestScore = -1;
    for (double b = lo; b < hi; b += 0.5) {
        const double sc = score(r, b, fr, span);
        if (sc > bestScore) {
            bestScore = sc;
            best = b;
        }
    }
    if (bestScore <= 0) return std::nullopt;
    // Половинный темп: двойной в диапазоне и почти так же хорош — двойной.
    const double twice = best * 2;
    if (twice < hi && score(r, twice, fr, span) >= 0.8 * bestScore) best = twice;
    double fine = best, fineScore = -1;
    for (double b = best - 0.6; b <= best + 0.6; b += 0.01) {
        const double sc = score(r, b, fr, span);
        if (sc > fineScore) {
            fineScore = sc;
            fine = b;
        }
    }
    const double whole = std::round(fine);
    if (std::fabs(fine - whole) < 0.2) return whole;
    return std::round(fine * 10) / 10;
}

double score(const std::vector<double>& r, double bpm, double fr, double span) {
    const double lag = 60 * fr / bpm;
    double sum = 0;
    int k = 1;
    while (double(k) * lag <= span) {
        sum += interp(r, lag * double(k));
        sum += 0.5 * interp(r, lag * (double(k) - 0.5));
        k += 1;
    }
    if (k == 1) return 0;
    const double oct = std::fabs(std::log2(bpm / 125));
    return sum / double(k - 1) * (1 - 0.15 * oct);
}

double interp(const std::vector<double>& r, double x) {
    const int i = int(x);
    if (i + 1 >= int(r.size()) || i < 0) return 0;
    const double f = x - double(i);
    return r[size_t(i)] * (1 - f) + r[size_t(i + 1)] * f;
}

std::vector<double> envelope(const SampleData& s, int from, int count) {
    const int n = count / hop;
    std::vector<double> full(size_t(n), 0), low(size_t(n), 0);
    const float* l = s.left();
    const float* r = s.right();
    const float a = 0.0194f;
    float lp = 0;
    for (int k = 0; k < n; ++k) {
        float e = 0, el = 0;
        for (int i = from + k * hop, end = i + hop; i < end; ++i) {
            const float m = (l[i] + r[i]) * 0.5f;
            lp += (m - lp) * a;
            e += m * m;
            el += lp * lp;
        }
        full[size_t(k)] = std::log(1 + 1000 * double(e));
        low[size_t(k)] = std::log(1 + 1000 * double(el));
    }
    std::vector<double> d(size_t(n), 0);
    for (int k = 1; k < n; ++k) {
        const double x = std::max(0.0, full[size_t(k)] - full[size_t(k - 1)]);
        const double y = std::max(0.0, low[size_t(k)] - low[size_t(k - 1)]);
        d[size_t(k)] = x + y;
    }
    // Минус скользящее среднее (±47 кадров).
    const int w = 47;
    std::vector<double> out(size_t(n), 0);
    double acc = 0;
    int lo2 = 0, hi2 = 0;
    for (int k = 0; k < n; ++k) {
        while (hi2 < std::min(n, k + w + 1)) acc += d[size_t(hi2++)];
        while (lo2 < k - w) acc -= d[size_t(lo2++)];
        const double mean = acc / double(hi2 - lo2);
        out[size_t(k)] = std::max(0.0, d[size_t(k)] - mean);
    }
    return out;
}

std::vector<double> autocorr(const std::vector<double>& x, int maxLag) {
    const int n = int(x.size());
    std::vector<double> r(size_t(maxLag + 1), 0);
    for (int lag = 0; lag <= maxLag; ++lag) {
        double sum = 0;
        const int m = n - lag;
        for (int i = 0; i < m; ++i) sum += x[size_t(i)] * x[size_t(i + lag)];
        r[size_t(lag)] = sum / double(std::max(1, m));
    }
    return r;
}

}  // namespace TempoDetect

namespace Onsets {

std::vector<double> detect(const SampleData& s) {
    const int h = hop;
    const int n = s.frames() / h;
    if (n < 4) return {};
    const float* l = s.left();
    const float* r = s.right();
    std::vector<double> e(size_t(n), 0);
    float prev = 0;
    for (int k = 0; k < n; ++k) {
        float a = 0;
        for (int i = k * h, end = i + h; i < end; ++i) {
            const float m = s.channels() > 1 ? (l[i] + r[i]) * 0.5f : l[i];
            const float d = m - prev;
            prev = m;
            a += m * m + 4 * d * d;
        }
        e[size_t(k)] = 10 * std::log10(1e-10 + double(a) / double(h));
    }
    const int back = std::max(2, int(0.05 * coreRate) / h);
    const int gap = back;
    std::vector<double> out;
    int last = -gap;
    for (int k = 1; k < n; ++k) {
        double sum = 0;
        int c = 0;
        for (int j = std::max(0, k - back); j < k; ++j) {
            sum += e[size_t(j)];
            c += 1;
        }
        const double base = sum / double(std::max(1, c));
        const double rise = e[size_t(k)] - base;
        const bool loud = e[size_t(k)] > -60;
        const double next = k + 1 < n ? e[size_t(k + 1)] - base : -1000;
        if (loud && rise > 9 && rise >= next && k - last >= gap) {
            out.push_back(start(s, k * h));
            last = k;
        }
    }
    return out;
}

double start(const SampleData& s, int at) {
    const int w = int(0.005 * coreRate);
    const int from = std::max(0, at - w);
    const int end = std::min(s.frames() - 16, at + 2 * hop);
    if (end <= from) return double(at);
    const float* l = s.left();
    const float* r = s.right();
    float peak = 0;
    for (int j = from; j < end + 16; ++j) peak = std::max(peak, std::fabs(l[j]) + std::fabs(r[j]));
    int best = at;
    for (int i = from; i <= end; i += 4) {
        float a = 0;
        for (int q = i; q < i + 16; ++q) a += std::fabs(l[q]) + std::fabs(r[q]);
        a /= 16;
        if (a > 0.25f * peak) break;
        if (a < 0.05f * peak) best = i + 16;
    }
    const int pre = int(0.001 * coreRate);
    return double(std::max(0, best - pre));
}

}  // namespace Onsets
}  // namespace jambo
