// Порт SpectrumDisplay (UI/EqSpectrum.swift) и EqPlot.curves (UI/EqPlot.swift), ветка swift JAMbo.
#include "EqDisplay.h"
#include "eq/EqCoeffs.h"
#include <algorithm>
#include <cmath>

namespace jambo {

void SpectrumDisplay::frame(const Spectra& s, int n, double dt, std::vector<float>& pre, std::vector<float>& post) {
    dt = std::min(0.1, std::max(0.0, dt));
    if (int(dPre_.size()) != n) {
        dPre_.assign(size_t(n), bottom);
        dPost_ = dPre_;
        dDiff_.assign(size_t(n), 0);
    }
    const float fl = fall * float(dt);
    const float k = float(1 - std::exp(-dt / diffTau));
    a_.analyze(s.pre);
    b_.analyze(s.post);
    const auto a = bands(a_, n);
    const auto b = bands(b_, n);
    pre.resize(size_t(n));
    post.resize(size_t(n));
    const float range = top - bottom;
    for (size_t i = 0; i < size_t(n); ++i) {
        dPre_[i] = a[i] > dPre_[i] ? a[i] : std::max(a[i], dPre_[i] - fl);
        dDiff_[i] += (b[i] - a[i] - dDiff_[i]) * k;
        dPost_[i] = std::max(bottom, dPre_[i] + dDiff_[i]);
        pre[i] = (top - dPre_[i]) / range;
        post[i] = (top - dPost_[i]) / range;
    }
}

std::vector<float> SpectrumDisplay::bands(const SpectrumAnalyzer& an, int n) {
    const int bins = SpectrumAnalyzer::bins;
    cum_[0] = 0;
    for (int i = 0; i < bins; ++i) cum_[size_t(i + 1)] = cum_[size_t(i)] + std::pow(10.0, double(an.db[size_t(i)]) / 10);
    const double binHz = 48000.0 / SpectrumAnalyzer::size;
    auto at = [&](double f) {
        const double p = std::min(double(bins), std::max(0.0, f / binHz + 0.5));
        const int i = std::min(bins - 1, int(p));
        const double a = cum_[size_t(i)], b = cum_[size_t(i + 1)];
        return a + (b - a) * (p - i);
    };
    const double band = std::pow(2.0, 1.0 / 12);
    std::vector<float> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double f = EqView::freq(n > 1 ? double(i) / (n - 1) : 0);
        const double f0 = f / band, f1 = f * band;
        const double w = std::max(1e-3, (f1 - f0) / binHz);
        const double p = (at(f1) - at(f0)) / w;
        out[size_t(i)] = std::max(bottom, float(10 * std::log10(std::max(1e-12, p))));
    }
    return out;
}

EqCurves eqCurves(const Eq& eq, int n, const std::vector<double>& offs) {
    std::vector<double> freqs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) freqs[size_t(i)] = std::min(24000.0, EqView::freq(n > 1 ? double(i) / (n - 1) : 0));
    EqCurves c;
    c.stereo.assign(size_t(n), 0);
    c.mid.assign(size_t(n), 0);
    c.side.assign(size_t(n), 0);
    for (size_t k = 0; k < eq.bands.size(); ++k) {
        if (!eq.bands[k] || !eq.bands[k]->on) continue;
        EqBand b = *eq.bands[k];
        if (k < offs.size() && offs[k] != 0) b.gain += offs[k];
        const auto r = EqCoeffs::response(b, freqs);
        if (b.ms == 0) c.hasStereo = true;
        if (b.ms == 1) c.hasMid = true;
        if (b.ms == 2) c.hasSide = true;
        for (size_t i = 0; i < size_t(n); ++i) {
            if (b.ms == 0) c.stereo[i] += r[i];
            if (b.ms != 2) c.mid[i] += r[i];
            if (b.ms != 1) c.side[i] += r[i];
        }
    }
    return c;
}

}  // namespace jambo
