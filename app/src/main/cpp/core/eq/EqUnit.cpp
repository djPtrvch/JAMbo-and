// EQ одного канала (порт Core/EQ/EqUnit.swift).
#include "EqUnit.h"
#include <algorithm>
#include <cmath>
#include "EqCoeffs.h"
#include "EqSnapshot.h"

namespace jambo {

EqUnit::EqUnit(double sampleRate, int ln)
    : lanes(ln), laneL(size_t(ln), nullptr), laneR(size_t(ln), nullptr), sr(sampleRate),
      decay(-1 / (tau * sampleRate)), kind(2 * bands, -1), nst(2 * bands, 1), msm(2 * bands, 0), tf(2 * bands, 0),
      tg(2 * bands, 0), tq(2 * bands, 0), sf(2 * bands, 0), sg(2 * bands, 0), sq(2 * bands, 0), el(2 * bands, 0),
      fresh(2 * bands, 0), changed(2 * bands, 0), moving(2 * bands, 0), co(2 * bands * 5, 0), coR(2 * bands * 5, 0),
      st(size_t(ln) * 2 * bands * bandState, 0), oldL(size_t(ln) * maxFrames, 0.f), oldR(size_t(ln) * maxFrames, 0.f),
      midBuf(size_t(ln) * maxFrames, 0.f), sideBuf(size_t(ln) * maxFrames, 0.f), det(sampleRate),
      selfL(maxFrames, 0.f), selfR(maxFrames, 0.f), dynL(quantum * 5, 0), dynR(quantum * 5, 0), live(size_t(ln), 0),
      ring(size_t(ln), 0) {}

void EqUnit::process(float* left, float* right, int frames, const double* p) {
    laneL[0] = left;
    laneR[0] = right;
    process(frames, p);
}

void EqUnit::process(int frames, const double* p) {
    const int n = std::min(frames, maxFrames);
    if ((fadePos < 0 || fadePos >= fadeFrames) && !same(p, cur)) startFade(p);
    setTargets(p, cur);
    if (laneL[0] && laneR[0]) {
        std::copy(laneL[0], laneL[0] + n, selfL.begin());
        std::copy(laneR[0], laneR[0] + n, selfR.begin());
    } else {
        std::fill(selfL.begin(), selfL.begin() + n, 0.f);
        std::fill(selfR.begin(), selfR.begin() + n, 0.f);
    }
    runDetectors(cur, n);
    if (fadePos >= 0) runDetectors(1 - cur, n);
    bool any = false;
    for (int k = 0; k < lanes; ++k) {
        const size_t K = size_t(k);
        live[K] = 0;
        if (laneL[K] && laneR[K] && (ring[K] != 0 || !silent(laneL[K], laneR[K], n))) {
            live[K] = 1;
            any = true;
        }
    }
    if (!any) {
        snap(cur);
        if (fadePos >= 0) endFade(n);
        return;
    }
    if (fadePos >= 0) {
        fadeIn(n);
        run(1 - cur, n, true);
    }
    run(cur, n, false);
    if (fadePos >= 0) {
        mixFade(n);
        endFade(n);
    }
    for (int k = 0; k < lanes; ++k)
        if (live[size_t(k)] != 0) ring[size_t(k)] = settle(k) ? 1 : 0;
}

void EqUnit::idle(const double* p, int frames) {
    if (fadePos >= 0 || !same(p, cur)) {
        startFade(p);
        fadePos = -1;
        std::fill(st.begin(), st.end(), 0.0);
    }
    setTargets(p, cur);
    snap(cur);
    const int n = std::min(frames, maxFrames);
    if (n > 0) {
        std::fill(selfL.begin(), selfL.begin() + n, 0.f);
        std::fill(selfR.begin(), selfR.begin() + n, 0.f);
        runDetectors(cur, n);
    }
}

bool EqUnit::same(const double* p, int c) const {
    for (int k = 0; k < bands; ++k) {
        const int o = 1 + k * EqSpec::bandStride;
        const int t = int(p[o]);
        const size_t i = size_t(c * bands + k);
        if (kind[i] != t) return false;
        if (t >= 0 && nst[i] != int(p[o + 1])) return false;
        if (t >= 0 && msm[i] != int(p[o + 5])) return false;
        if (det.src[i] != (t >= 0 ? int(p[o + 6]) : -1)) return false;
    }
    return true;
}

void EqUnit::startFade(const double* p) {
    const int c = 1 - cur;
    for (int k = 0; k < bands; ++k) {
        const size_t i = size_t(c * bands + k);
        const int o = 1 + k * EqSpec::bandStride;
        kind[i] = int(p[o]);
        nst[i] = std::max(1, std::min(maxStages, int(p[o + 1])));
        tf[i] = p[o + 2];
        tg[i] = p[o + 3];
        tq[i] = p[o + 4];
        msm[i] = int(p[o + 5]);
        moving[i] = 0;
        changed[i] = 0;
        if (kind[i] >= 0) EqCoeffs::coeffs(kind[i], tf[i], tg[i], tq[i], sr, co.data() + i * 5);
        det.reset(int(i), p + o, kind[i], sr);
    }
    const size_t m = size_t(bands * bandState);
    for (int lane = 0; lane < lanes; ++lane) {
        auto b = st.begin() + long(size_t(lane * 2 + c) * m);
        std::fill(b, b + long(m), 0.0);
    }
    cur = c;
    fadePos = 0;
}

void EqUnit::setTargets(const double* p, int c) {
    for (int k = 0; k < bands; ++k) {
        const size_t i = size_t(c * bands + k);
        const int o = 1 + k * EqSpec::bandStride;
        const double f = p[o + 2], g = p[o + 3], q = p[o + 4];
        det.setLevels(int(i), p + o);
        if (kind[i] >= 0 && kind[i] == int(p[o]) && (f != tf[i] || g != tg[i] || q != tq[i])) {
            det.retarget(int(i), p + o, kind[i], decay);
            // Новое сглаживание: от значения, до которого дошло прежнее.
            if (moving[i] != 0) {
                const double e = std::exp(double(el[i]) * decay);
                sf[i] = tf[i] + (sf[i] - tf[i]) * e;
                sg[i] = tg[i] + (sg[i] - tg[i]) * e;
                sq[i] = tq[i] + (sq[i] - tq[i]) * e;
            } else {
                sf[i] = tf[i];
                sg[i] = tg[i];
                sq[i] = tq[i];
            }
            tf[i] = f;
            tg[i] = g;
            tq[i] = q;
            el[i] = 0;
            fresh[i] = changed[i] == 0 ? 1 : 0;
            changed[i] = 1;
            moving[i] = 1;
        }
    }
}

void EqUnit::snap(int c) {
    for (int k = 0; k < bands; ++k) {
        const size_t i = size_t(c * bands + k);
        if (kind[i] >= 0 && moving[i] != 0) {
            moving[i] = 0;
            EqCoeffs::coeffs(kind[i], tf[i], tg[i], tq[i], sr, co.data() + i * 5);
        }
    }
}

bool EqUnit::silent(const float* l, const float* r, int n) {
    for (int i = 0; i < n; ++i)
        if (l[i] != 0 || r[i] != 0) return false;
    return true;
}

bool EqUnit::settle(int lane) {
    const size_t m = size_t(2 * bands * bandState);
    double* s = st.data() + size_t(lane) * m;
    double peak = 0;
    for (size_t i = 0; i < m; ++i) {
        const double v = std::abs(s[i]);
        if (v > peak) peak = v;
    }
    if (peak < quiet) {
        if (peak > 0) std::fill(s, s + m, 0.0);
        return false;
    }
    return true;
}

void EqUnit::fadeIn(int n) {
    const float total = float(fadeFrames);
    for (int lane = 0; lane < lanes; ++lane) {
        float* l = laneL[size_t(lane)];
        float* r = laneR[size_t(lane)];
        if (live[size_t(lane)] == 0 || !l || !r) continue;
        float* fl = oldL.data() + lane * maxFrames;
        float* fr = oldR.data() + lane * maxFrames;
        int pos = fadePos;
        for (int i = 0; i < n; ++i) {
            const float g = pos >= fadeFrames ? 1 : float(pos) / total;
            fl[i] = l[i] * (1 - g);
            fr[i] = r[i] * (1 - g);
            l[i] *= g;
            r[i] *= g;
            ++pos;
        }
    }
}

void EqUnit::mixFade(int n) {
    for (int lane = 0; lane < lanes; ++lane) {
        float* l = laneL[size_t(lane)];
        float* r = laneR[size_t(lane)];
        if (live[size_t(lane)] == 0 || !l || !r) continue;
        const float* fl = oldL.data() + lane * maxFrames;
        const float* fr = oldR.data() + lane * maxFrames;
        for (int i = 0; i < n; ++i) {
            l[i] += fl[i];
            r[i] += fr[i];
        }
    }
}

void EqUnit::endFade(int n) {
    const int pos = fadePos + n;
    if (pos < fadeFrames) {
        fadePos = pos;
        return;
    }
    fadePos = fadeFrames;
    const size_t m = size_t(bands * bandState);
    double peak = 0;
    for (int lane = 0; lane < lanes; ++lane) {
        const double* s = st.data() + size_t(lane * 2 + 1 - cur) * m;
        for (size_t i = 0; i < m; ++i) {
            const double v = std::abs(s[i]);
            if (v > peak) peak = v;
        }
    }
    if (peak >= quiet) return;
    fadePos = -1;
    for (int lane = 0; lane < lanes; ++lane) {
        double* s = st.data() + size_t(lane * 2 + 1 - cur) * m;
        std::fill(s, s + m, 0.0);
    }
}

void EqUnit::run(int c, int n, bool old) {
    for (int k = 0; k < bands; ++k) {
        const int i = c * bands + k;
        if (kind[size_t(i)] < 0) continue;
        if (msm[size_t(i)] == 0)
            runBand(c, i, n, old);
        else
            runMsBand(c, i, n, old);
    }
}

bool EqUnit::signal(int lane, bool old, float*& l, float*& r) {
    if (live[size_t(lane)] == 0) return false;
    if (old) {
        l = oldL.data() + lane * maxFrames;
        r = oldR.data() + lane * maxFrames;
        return true;
    }
    l = laneL[size_t(lane)];
    r = laneR[size_t(lane)];
    return l && r;
}

void EqUnit::runBand(int c, int i, int n, bool old) {
    const size_t I = size_t(i);
    const int stages = nst[I];
    const int k = i - c * bands;
    double* cp = co.data() + I * 5;
    double* cr = coR.data() + I * 5;
    const size_t laneStride = size_t(2 * bands * bandState);
    double* s0 = st.data() + size_t(c * bands + k) * bandState;
    const bool dyn = det.src[I] >= 0 && det.live[I] != 0;
    const float* off = det.off.data() + I * maxFrames;
    float* l;
    float* r;
    int a = 0;
    while (a < n) {
        const int m = std::min(quantum, n - a);
        const bool dq = dyn && nonZero(off + a, m);
        if (moving[I] == 0 && !dq) {
            if (det.dirty[I] != 0) {
                det.dirty[I] = 0;
                EqCoeffs::coeffs(kind[I], tf[I], tg[I], tq[I], sr, cp);
            }
            for (int lane = 0; lane < lanes; ++lane) {
                if (!signal(lane, old, l, r)) continue;
                double* z = s0 + size_t(lane) * laneStride;
                for (int s = 0; s < stages; ++s) {
                    EqDsp::biquad(l + a, m, cp, z + s * 8);
                    EqDsp::biquad(r + a, m, cp, z + s * 8 + 4);
                }
            }
            a += m;
            continue;
        }
        // Отрезки траектории L и R в этом кванте.
        const bool mov = moving[I] != 0;
        const int eL = el[I];
        const int eR = fresh[I] != 0 ? eL : eL + quantum;
        if (mov) {
            el[I] += fresh[I] != 0 ? quantum : 2 * quantum;
            fresh[I] = 0;
        }
        if (dq) {
            // Сдвиг Dynamic: коэффициенты на каждый отсчёт.
            det.dirty[I] = 1;
            fillCoeffs(i, mov, eL, off + a, m, dynL.data());
            const bool sm = !mov || eR == eL;
            if (!sm) fillCoeffs(i, mov, eR, off + a, m, dynR.data());
            const double* cR = sm ? dynL.data() : dynR.data();
            for (int lane = 0; lane < lanes; ++lane) {
                if (!signal(lane, old, l, r)) continue;
                double* z = s0 + size_t(lane) * laneStride;
                for (int s = 0; s < stages; ++s) {
                    EqDsp::biquadVar(l + a, m, dynL.data(), z + s * 8);
                    EqDsp::biquadVar(r + a, m, cR, z + s * 8 + 4);
                }
            }
        } else {
            // Коэффициенты — точно на краях кусков sub, между ними — линейно по отсчётам.
            double* dl = ramp;
            double* dr = ramp + 5;
            coeffsAt(i, double(eL), cp);
            coeffsAt(i, double(eR), cr);
            int j = 0;
            while (j < m) {
                const int w = std::min(sub, m - j);
                coeffsAt(i, double(eL + j + w), dl);
                coeffsAt(i, double(eR + j + w), dr);
                const double inv = 1 / double(w);
                for (int t = 0; t < 5; ++t) {
                    dl[t] = (dl[t] - cp[t]) * inv;
                    dr[t] = (dr[t] - cr[t]) * inv;
                }
                for (int lane = 0; lane < lanes; ++lane) {
                    if (!signal(lane, old, l, r)) continue;
                    double* z = s0 + size_t(lane) * laneStride;
                    for (int s = 0; s < stages; ++s) {
                        EqDsp::biquadRamp(l + a + j, w, cp, dl, z + s * 8);
                        EqDsp::biquadRamp(r + a + j, w, cr, dr, z + s * 8 + 4);
                    }
                }
                for (int t = 0; t < 5; ++t) {
                    cp[t] += dl[t] * double(w);
                    cr[t] += dr[t] * double(w);
                }
                j += w;
            }
        }
        // Разница с целью меньше 1e-7 — сглаживание кончилось.
        if (mov && double(el[I]) * decay < -16.2) {
            moving[I] = 0;
            EqCoeffs::coeffs(kind[I], tf[I], tg[I], tq[I], sr, cp);
        }
        a += m;
    }
}

void EqUnit::coeffsAt(int i, double e, double* out) {
    const size_t I = size_t(i);
    const double x = std::exp(e * decay);
    const double f = tf[I] + (sf[I] - tf[I]) * x;
    const double g = tg[I] + (sg[I] - tg[I]) * x;
    const double q = tq[I] + (sq[I] - tq[I]) * x;
    EqCoeffs::coeffs(kind[I], f, g, q, sr, out);
}

}  // namespace jambo
