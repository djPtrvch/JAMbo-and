#include "MasterWin.h"
#include <algorithm>
#include <cmath>
#include "EqDisplay.h"

namespace jambo {

TeleHistory::TeleHistory() : sIn(DynTele::capacity, 0.f), sGr(DynTele::capacity, 0.f), sOut(DynTele::capacity, 0.f) {}

void TeleHistory::setFrozen(bool on, const DynTele& t) {
    frozen = on;
    if (!on) return;
    sIn = t.inPeak;
    sGr = t.gr;
    sOut = t.outPeak;
    sWritten = t.written.load();
}

std::optional<TeleHistory::Latest> TeleHistory::latest(const DynTele& t, double now) const {
    const long w = t.written.load();
    if (w == 0 || !isLive(now)) return std::nullopt;
    const size_t q = size_t((w - 1) & (DynTele::capacity - 1));
    return Latest{t.inPeak[q], t.det[q], t.gr[q]};
}

TeleHistory::Columns TeleHistory::columns(const DynTele& t, int cols, double now) {
    const long w = t.written.load();
    if (w != lastWritten) {
        lastWritten = w;
        lastAt = now;
    }
    const int n = std::max(0, cols);
    Columns c{std::vector<float>(size_t(n), -200.f), std::vector<float>(size_t(n), -200.f), std::vector<float>(size_t(n), 0.f)};
    const bool live = frozen || now - lastAt < liveTime;
    if (!live || n == 0) return c;
    const std::vector<float>& pIn = frozen ? sIn : t.inPeak;
    const std::vector<float>& pGr = frozen ? sGr : t.gr;
    const std::vector<float>& pOut = frozen ? sOut : t.outPeak;
    const long end = frozen ? sWritten : w;
    const long f = frames;
    const long mask = DynTele::capacity - 1;
    const long lo = std::max(0L, end - DynTele::capacity + 1);
    for (int x = 0; x < n; x++) {
        const long a = end - f + long(x) * f / n;
        const long b = end - f + long(x + 1) * f / n;
        long k = std::max(a, lo);
        const long stop = std::min(std::max(b, a + 1), end);
        float mi = 0, mo = 0, mg = 0;
        for (; k < stop; k++) {
            const size_t q = size_t(k & mask);
            mi = std::max(mi, pIn[q]);
            mo = std::max(mo, pOut[q]);
            mg = std::max(mg, pGr[q]);
        }
        c.inDb[size_t(x)] = mi > 0 ? 20 * std::log10(mi) : -200;
        c.outDb[size_t(x)] = mo > 0 ? 20 * std::log10(mo) : -200;
        c.gr[size_t(x)] = mg;
    }
    return c;
}

void SatSpectrumState::update(const MasterChain& m, int n, double now) {
    n = std::max(2, n);
    if (int(dIn.size()) != n) {
        dIn.assign(size_t(n), bottom);
        dOut = dIn;
    }
    const double dt = last > 0 ? std::min(0.1, now - last) : 0;
    last = now;
    if (frozen) return;
    const float fall = 30 * float(dt);
    pre.analyze(m.satIn);
    post.analyze(m.satOut);
    const auto a = bands(pre, n);
    const auto b = bands(post, n);
    for (size_t i = 0; i < size_t(n); i++) {
        dIn[i] = a[i] > dIn[i] ? a[i] : std::max(a[i], dIn[i] - fall);
        dOut[i] = b[i] > dOut[i] ? b[i] : std::max(b[i], dOut[i] - fall);
    }
}

std::vector<float> SatSpectrumState::bands(const SpectrumAnalyzer& an, int n) {
    const int bins = SpectrumAnalyzer::bins;
    cum[0] = 0;
    for (int i = 0; i < bins; i++) cum[size_t(i + 1)] = cum[size_t(i)] + std::pow(10.0, double(an.db[size_t(i)]) / 10);
    const double binHz = 48000.0 / SpectrumAnalyzer::size;
    auto at = [&](double f) {
        const double p = std::min(double(bins), std::max(0.0, f / binHz + 0.5));
        const int i = std::min(bins - 1, int(p));
        return cum[size_t(i)] + (cum[size_t(i + 1)] - cum[size_t(i)]) * (p - i);
    };
    const double band = std::pow(2.0, 1.0 / 12);
    std::vector<float> out(size_t(n), bottom);
    for (int i = 0; i < n; i++) {
        const double f = EqView::freq(double(i) / (n - 1));
        const double f0 = f / band, f1 = f * band;
        const double w = std::max(1e-3, (f1 - f0) / binHz);
        const double p = (at(f1) - at(f0)) / w;
        out[size_t(i)] = std::max(bottom, float(10 * std::log10(std::max(1e-12, p))));
    }
    return out;
}

}  // namespace jambo
