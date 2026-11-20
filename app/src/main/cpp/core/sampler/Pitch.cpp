// Порт Core/Sampler/Pitch.swift (ветка swift JAMbo).
#include "Pitch.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace jambo {
namespace PitchDetect {

static constexpr double minHz = 40;
static constexpr double maxHz = 4200;
static constexpr int window = 2048;
static constexpr int shortWindow = 1024;
static constexpr float threshold = 0.15f;
static constexpr double sampleRate = 48000;

std::optional<int> root(const SampleData& s) {
    const int step = 2;
    const double rate = sampleRate / double(step);
    const int n = s.frames() / step;
    int win = n >= window * 2 ? window : shortWindow;
    if (n < shortWindow) return std::nullopt;
    std::vector<float> data(static_cast<size_t>(n), 0.f);
    const float k = s.channels() == 2 ? 0.5f : 1.f;
    const float* L = s.left();
    const float* R = s.right();
    for (int i = 0; i < n; i++) {
        const int j = i * step;
        const float l = L[j] + L[j + 1];
        data[size_t(i)] = s.channels() == 2 ? (l + R[j] + R[j + 1]) * k * 0.5f : l * 0.5f;
    }
    int peakAt = 0;
    float peak = 0;
    const int scan = std::min(n, int(rate * 0.3));
    for (int i = 0; i < scan; i++)
        if (std::fabs(data[size_t(i)]) > peak) {
            peak = std::fabs(data[size_t(i)]);
            peakAt = i;
        }
    if (peak < 1e-4f) return std::nullopt;
    const int start0 = std::min(n - 1, peakAt + int(rate * 0.02));
    int loudEnd = start0;
    const int limit = std::min(n, start0 + int(rate * 1.5) + window);
    for (int i0 = start0; i0 < limit; i0++)
        if (std::fabs(data[size_t(i0)]) >= peak * 0.1f) loudEnd = i0;
    if (loudEnd - start0 < window * 3 / 2) win = shortWindow;
    const int last = n - win;
    if (last < 0) return std::nullopt;
    const int from = std::max(0, std::min(start0, last));
    const int to = std::max(from, std::min(last, loudEnd - win / 2));
    std::vector<int> notes;
    for (int w = 0; w < 8; w++) {
        const int start = from + (to - from) * w / 8;
        float e = 0;
        for (int i = 0; i < win; i++) e = std::max(e, std::fabs(data[size_t(start + i)]));
        if (e < peak * 0.05f) continue;
        if (auto hz = yin(data, start, rate, win)) notes.push_back(int(std::round(69 + 12 * std::log2(*hz / 440))));
    }
    if (notes.size() < 3) return std::nullopt;
    std::map<int, int> count;
    for (int v : notes) count[v]++;
    int best = notes[0];
    int votes = 0;
    for (int v : notes)
        if (count[v] > votes) {
            best = v;
            votes = count[v];
        }
    const int need = (int(notes.size()) + 1) / 2;
    if (votes < need) return std::nullopt;
    return std::min(108, std::max(12, best));
}

std::optional<double> yin(const std::vector<float>& x, int start, double rate, int win) {
    const int minTau = int(rate / maxHz);
    const int maxTau = std::min(int(rate / minHz), win / 2);
    std::vector<float> d(static_cast<size_t>(maxTau + 2), 0.f);
    for (int tau = 1; tau <= maxTau; tau++) {
        float s = 0;
        for (int i = 0; i < win - maxTau; i++) {
            const float diff = x[size_t(start + i)] - x[size_t(start + i + tau)];
            s += diff * diff;
        }
        d[size_t(tau)] = s;
    }
    float sum = 0;
    for (int tau = 1; tau <= maxTau; tau++) {
        sum += d[size_t(tau)];
        const float cmnd = sum > 0 ? d[size_t(tau)] * float(tau) / sum : 1;
        if (tau >= minTau && cmnd < threshold) {
            int t = tau;
            while (t + 1 <= maxTau && d[size_t(t + 1)] < d[size_t(t)]) t++;
            const float a = d[size_t(t - 1)];
            const float b = d[size_t(t)];
            const float c = t + 1 <= maxTau ? d[size_t(t + 1)] : d[size_t(t)];
            const float den = 2 * (a - 2 * b + c);
            const float shift = den != 0 ? (a - c) / den : 0;
            return rate / (double(t) + (std::isfinite(shift) ? double(shift) : 0));
        }
    }
    return std::nullopt;
}

}  // namespace PitchDetect
}  // namespace jambo
