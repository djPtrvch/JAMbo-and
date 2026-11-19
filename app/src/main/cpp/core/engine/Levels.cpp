// Уровни и громкость LUFS (порт Core/Engine/Levels.swift).
#include "Levels.h"
#include <algorithm>
#include <cmath>
#include "eq/EqCoeffs.h"

namespace jambo {

namespace {
constexpr float minusInf = -std::numeric_limits<float>::infinity();
}

Loudness::Loudness(double sampleRate)
    : energy(ringBlocks, 0), frames(ringBlocks, 0), binN(bins, 0), binE(bins, 0), zeros(maxFrames, 0.f) {
    if (sampleRate == 48000) {
        // BS.1770-4, таблицы 1 и 2: предфильтр (полка) и RLB (ФВЧ) для 48 кГц.
        const double k[10] = {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241,
                              0.73248077421585, 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
        std::copy(k, k + 10, c);
    } else {
        EqCoeffs::coeffs(EqSpec::highShelf, 1682, 4, 0, sampleRate, c);
        EqCoeffs::coeffs(EqSpec::lowCut, 38, 0, 0.5, sampleRate, c + 5);
    }
    source.store(-1);
}

bool Loudness::listens(int ch) {
    const long s = source.load();
    if (s != lastSource) {
        lastSource = s;
        restart();
    }
    const long r = resetRequests.load();
    if (r != lastReset) {
        lastReset = r;
        clearIntegrated();
    }
    return s == ch && s >= 0;
}

void Loudness::restart() {
    std::fill(z, z + 16, 0.0);
    std::fill(energy.begin(), energy.end(), 0.0);
    std::fill(frames.begin(), frames.end(), 0);
    sinceHop = 0;
    momentary.store(minusInf);
    shortTerm.store(minusInf);
    clearIntegrated();
}

void Loudness::clearIntegrated() {
    std::fill(binN.begin(), binN.end(), 0.0);
    std::fill(binE.begin(), binE.end(), 0.0);
    integrated.store(minusInf);
}

void Loudness::process(const float* l, const float* r, int n) {
    fed = true;
    const double e = filter(l, z, n) + filter(r, z + 8, n);
    push(e, n);
}

void Loudness::end(int n) {
    if (!fed && listens(int(source.load()))) {
        const double zero = filterSilence(z, n) + filterSilence(z + 8, n);
        push(zero, n);
    }
    fed = false;
}

double Loudness::filter(const float* x, double* s, int n) {
    const double a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3], a4 = c[4];
    const double b0 = c[5], b1 = c[6], b2 = c[7], b3 = c[8], b4 = c[9];
    double x1 = s[0], x2 = s[1], y1 = s[2], y2 = s[3];
    double u1 = s[4], u2 = s[5], v1 = s[6], v2 = s[7];
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        const double xi = double(x[i]);
        const double y = a0 * xi + a1 * x1 + a2 * x2 - a3 * y1 - a4 * y2;
        x2 = x1;
        x1 = xi;
        y2 = y1;
        y1 = y;
        const double v = b0 * y + b1 * u1 + b2 * u2 - b3 * v1 - b4 * v2;
        u2 = u1;
        u1 = y;
        v2 = v1;
        v1 = v;
        sum += v * v;
    }
    // Денормалы после долгой тишины.
    if (std::abs(y1) < 1e-30 && std::abs(v1) < 1e-30) {
        y1 = 0;
        y2 = 0;
        v1 = 0;
        v2 = 0;
    }
    s[0] = x1;
    s[1] = x2;
    s[2] = y1;
    s[3] = y2;
    s[4] = u1;
    s[5] = u2;
    s[6] = v1;
    s[7] = v2;
    return sum;
}

double Loudness::filterSilence(double* s, int n) {
    if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0 && s[6] == 0 && s[7] == 0) return 0;
    return filter(zeros.data(), s, std::min(n, maxFrames));
}

void Loudness::push(double e, int n) {
    const size_t k = size_t(written % ringBlocks);
    energy[k] = e;
    frames[k] = n;
    ++written;
    sinceHop += n;
    if (sinceHop < hop) return;
    sinceHop = 0;
    const double ms = mean(window);
    momentary.store(ms > 1e-10 ? float(-0.691 + 10 * std::log10(ms)) : minusInf);
    const double st = mean(shortWindow);
    shortTerm.store(st > 1e-10 ? float(-0.691 + 10 * std::log10(st)) : minusInf);
    integrate(ms);
}

double Loudness::mean(int span) const {
    double sum = 0;
    int got = 0;
    long j = written - 1;
    while (j >= 0 && j > written - 1 - ringBlocks && got < span) {
        const size_t q = size_t(j % ringBlocks);
        sum += energy[q];
        got += frames[q];
        --j;
    }
    return sum / double(std::max(got, span));
}

void Loudness::integrate(double e) {
    const double l = e > 1e-10 ? -0.691 + 10 * std::log10(e) : -std::numeric_limits<double>::infinity();
    if (l <= absGate) return;
    const int b = std::min(bins - 1, int(std::floor((l - absGate) * 10)));
    binN[size_t(b)] += 1;
    binE[size_t(b)] += e;
    double n = 0, s = 0;
    for (int i = 0; i < bins; ++i) {
        n += binN[size_t(i)];
        s += binE[size_t(i)];
    }
    const double rel = -0.691 + 10 * std::log10(s / n) - 10;
    int i = std::max(0, int(std::ceil((rel - absGate) * 10)));
    n = 0;
    s = 0;
    for (; i < bins; ++i) {
        n += binN[size_t(i)];
        s += binE[size_t(i)];
    }
    integrated.store(n > 0 ? float(-0.691 + 10 * std::log10(s / n)) : minusInf);
}

double Loudness::integratedOf(const float* left, const float* right, int fr) {
    Loudness m(48000);
    m.source.store(0);
    (void)m.listens(0);
    for (int at = 0; at < fr; at += hop) {
        const int n = std::min(hop, fr - at);
        m.process(left + at, right + at, n);
        m.end(n);
    }
    return double(m.integrated.load());
}

Levels::Levels() : data(size_t(ring * channels * stride), 0.f) { mixLoudness.source.store(master); }

float* Levels::begin() {
    float* p = data.data() + size_t((blocks.load() % ring) * channels * stride);
    std::fill(p, p + channels * stride, 0.f);
    return p;
}

void Levels::commit(int fr) {
    loudness.end(fr);
    mixLoudness.end(fr);
    blocks.store(blocks.load() + 1);
}

long Levels::read(long since, const std::function<void(const float*)>& f) const {
    const long w = blocks.load();
    for (long k = std::max(since, w - ring + 1); k < w; ++k) f(data.data() + size_t((k % ring) * channels * stride));
    return w;
}

}  // namespace jambo
