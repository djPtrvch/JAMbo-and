// Детекторы динамических полос EQ (порт Core/EQ/EqDyn.swift).
#include "EqDyn.h"
#include <algorithm>
#include <cmath>
#include "EqCoeffs.h"
#include "EqUnit.h"

namespace jambo {

EqDetectors::EqDetectors(double sr)
    : kAtt(std::exp(-1 / (attackMs / 1000 * sr))), kRel(std::exp(-1 / (releaseMs / 1000 * sr))), src(n, -1), ms(n, 0),
      thr(n, 0), range(n, 0), kind(n, 0), tf(n, 0), tq(n, 0), sf(n, 0), sq(n, 0), el(n, 0), fresh(n, 0), changed(n, 0),
      moving(n, 0), co(n * 5, 0), coR(n * 5, 0), st(n * 8, 0), env(n, 0), off(size_t(n) * EqDsp::maxFrames, 0.f),
      last(n, 0.f), live(n, 0), dirty(n, 0), bufL(EqDsp::maxFrames, 0.f), bufR(EqDsp::maxFrames, 0.f) {}

int EqDetectors::detKind(int band) {
    if (band == EqSpec::lowShelf) return EqSpec::highCut;
    if (band == EqSpec::highShelf) return EqSpec::lowCut;
    return bandpass;
}

void EqDetectors::reset(int i, const double* p, int band, double sr) {
    const size_t I = size_t(i);
    const int s = band >= 0 ? int(p[6]) : -1;
    src[I] = s;
    moving[I] = 0;
    changed[I] = 0;
    dirty[I] = 0;
    live[I] = 0;
    env[I] = 0;
    last[I] = 0;
    std::fill(st.begin() + long(I * 8), st.begin() + long(I * 8 + 8), 0.0);
    std::fill(off.begin() + long(I) * EqDsp::maxFrames, off.begin() + long(I + 1) * EqDsp::maxFrames, 0.f);
    if (s < 0) return;
    kind[I] = detKind(band);
    ms[I] = int(p[5]);
    thr[I] = p[7];
    range[I] = p[8];
    tf[I] = p[2];
    tq[I] = band == EqSpec::bell ? p[4] : shelfQ;
    EqCoeffs::coeffs(kind[I], tf[I], 0, tq[I], sr, co.data() + I * 5);
}

void EqDetectors::retarget(int i, const double* p, int band, double decay) {
    const size_t I = size_t(i);
    if (src[I] < 0) return;
    if (moving[I] != 0) {
        const double e = std::exp(double(el[I]) * decay);
        sf[I] = tf[I] + (sf[I] - tf[I]) * e;
        sq[I] = tq[I] + (sq[I] - tq[I]) * e;
    } else {
        sf[I] = tf[I];
        sq[I] = tq[I];
    }
    tf[I] = p[2];
    tq[I] = band == EqSpec::bell ? p[4] : shelfQ;
    el[I] = 0;
    fresh[I] = changed[I] == 0 ? 1 : 0;
    changed[I] = 1;
    moving[I] = 1;
}

bool EqDetectors::still(const double* z) {
    for (int k = 0; k < 8; ++k) {
        const double v = z[k] < 0 ? -z[k] : z[k];
        if (v >= EqDsp::quiet) return false;
    }
    return true;
}

void EqDetectors::run(int i, const float* l, const float* r, int nn, double sr, double decay) {
    const size_t I = size_t(i);
    float* o = off.data() + I * EqDsp::maxFrames;
    double* z = st.data() + I * 8;
    float* fl = bufL.data();
    float* fr = bufR.data();
    bool quiet = true;
    if (l && r) {
        std::copy(l, l + nn, fl);
        std::copy(r, r + nn, fr);
        for (int j = 0; j < nn; ++j) {
            if (fl[j] != 0 || fr[j] != 0) {
                quiet = false;
                break;
            }
        }
    } else {
        std::fill(fl, fl + nn, 0.f);
        std::fill(fr, fr + nn, 0.f);
    }
    // Тишина, затихший фильтр и огибающая ниже −120 dB: сдвиг 0, считать нечего.
    if (quiet && env[I] < 1e-6 && still(z) && moving[I] == 0) {
        std::fill(z, z + 8, 0.0);
        env[I] = 0;
        if (live[I] != 0 || last[I] != 0) std::fill(o, o + nn, 0.f);
        live[I] = 0;
        last[I] = 0;
        return;
    }
    filter(i, fl, fr, nn, sr, decay);
    const double ka = kAtt, kr = kRel;
    const double t = thr[I], rg = range[I];
    const int mode = ms[I];
    const double lim = std::pow(10.0, t / 20) - 1e-12;
    const double span = spanDb;
    double e = env[I];
    bool any = false;
    float v = 0;
    for (int j = 0; j < nn; ++j) {
        const double a0 = double(fl[j]);
        const double b0 = double(fr[j]);
        double a;
        if (mode == 1) {
            const double s = a0 + b0;
            a = (s < 0 ? -s : s) * 0.5;
        } else if (mode == 2) {
            const double s = a0 - b0;
            a = (s < 0 ? -s : s) * 0.5;
        } else {
            const double x = a0 < 0 ? -a0 : a0;
            const double y = b0 < 0 ? -b0 : b0;
            a = x > y ? x : y;
        }
        e = a + (e - a) * (a > e ? ka : kr) + 1e-25;
        v = 0;
        if (e > lim) {
            const double over = 20 * std::log10(e + 1e-12) - t;
            if (over > 0) {
                const double u = over / span;
                v = float(rg * (u < 1 ? u : 1));
            }
        }
        if (v != 0) any = true;
        o[j] = v;
    }
    env[I] = e;
    live[I] = any ? 1 : 0;
    last[I] = v;
}

void EqDetectors::filter(int i, float* l, float* r, int nn, double sr, double decay) {
    const size_t I = size_t(i);
    double* cp = co.data() + I * 5;
    double* cr = coR.data() + I * 5;
    double* z = st.data() + I * 8;
    const int q = EqDsp::quantum;
    int a = 0;
    while (a < nn) {
        const int m = std::min(q, nn - a);
        if (moving[I] == 0) {
            EqDsp::biquad(l + a, m, cp, z);
            EqDsp::biquad(r + a, m, cp, z + 4);
            a += m;
            continue;
        }
        const int eL = el[I];
        const int eR = fresh[I] != 0 ? eL : eL + q;
        el[I] += fresh[I] != 0 ? q : 2 * q;
        fresh[I] = 0;
        double* dl = ramp;
        double* dr = ramp + 5;
        coeffsAt(i, double(eL), sr, decay, cp);
        coeffsAt(i, double(eR), sr, decay, cr);
        int j = 0;
        while (j < m) {
            const int w = std::min(EqDsp::sub, m - j);
            coeffsAt(i, double(eL + j + w), sr, decay, dl);
            coeffsAt(i, double(eR + j + w), sr, decay, dr);
            const double inv = 1 / double(w);
            for (int t = 0; t < 5; ++t) {
                dl[t] = (dl[t] - cp[t]) * inv;
                dr[t] = (dr[t] - cr[t]) * inv;
            }
            EqDsp::biquadRamp(l + a + j, w, cp, dl, z);
            EqDsp::biquadRamp(r + a + j, w, cr, dr, z + 4);
            for (int t = 0; t < 5; ++t) {
                cp[t] += dl[t] * double(w);
                cr[t] += dr[t] * double(w);
            }
            j += w;
        }
        if (double(el[I]) * decay < -16.2) {
            moving[I] = 0;
            EqCoeffs::coeffs(kind[I], tf[I], 0, tq[I], sr, cp);
        }
        a += m;
    }
}

void EqDetectors::coeffsAt(int i, double e, double sr, double decay, double* out) {
    const size_t I = size_t(i);
    const double x = std::exp(e * decay);
    const double f = tf[I] + (sf[I] - tf[I]) * x;
    const double q = tq[I] + (sq[I] - tq[I]) * x;
    EqCoeffs::coeffs(kind[I], f, 0, q, sr, out);
}

void EqDynLevels::publish(int ch, const EqUnit& u) {
    for (int k = 0; k < EqSpec::bands; ++k) v[size_t(ch * EqSpec::bands + k)].store(u.dynOffset(k));
}

}  // namespace jambo
