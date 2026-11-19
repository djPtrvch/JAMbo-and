// Таймстретч NORMAL — фазовый вокодер (порт Core/Sampler/StretchPV.swift).
#include "StretchPV.h"
#include "Stretch.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace jambo {

Vocoder::Vocoder(int n)
    : outL(size_t(Stretcher::maxChunk), 0.f), outR(size_t(Stretcher::maxChunk), 0.f), slots(n), fft(size),
      win(size_t(size)), winG(size_t(size)), firstW(size_t(size)), omg(size_t(bins)), ringL(size_t(n * ring), 0.f),
      ringR(size_t(n * ring), 0.f), ringAt(size_t(n), 0), hopLeft(size_t(n), 0), primed(size_t(n), 0), lead(size_t(n), 0),
      late(size_t(n), 0), nextAt(size_t(n), INT64_MIN), prevAna(size_t(n * bins), 0.f), prevSyn(size_t(n * bins), 0.f),
      prevMag(size_t(n * bins), 0.f), instF(size_t(n * bins), 0.f), prevC(size_t(n), 0.0), prevHit(size_t(n), 0),
      lastDir(size_t(n), 1.0), segLo(size_t(n), 0.0), segHi(size_t(n), 0.0), fL(size_t(size), 0.f),
      fR(size_t(size), 0.f), reL(size_t(bins), 0.f), imL(size_t(bins), 0.f), reR(size_t(bins), 0.f),
      imR(size_t(bins), 0.f), reM(size_t(bins), 0.f), imM(size_t(bins), 0.f), mag(size_t(bins), 0.f),
      ph(size_t(bins), 0.f), syn(size_t(bins), 0.f), dc(size_t(bins), 0.f), ds(size_t(bins), 0.f),
      peaks(size_t(bins), 0), cub(size) {
    const int N = size, H = hop;
    for (int i = 0; i < N; ++i) win[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(N)));
    for (int i = 0; i < N; ++i) {
        float c = 0;
        for (int k = 0; i + k * H < N; ++k) {
            const float w = win[size_t(i + k * H)];
            c += w * w;
        }
        firstW[size_t(i)] = c / 1.5f;
    }
    norm = 1 / (fft.gain() * 1.5f);
    for (int i = 0; i < N; ++i) winG[size_t(i)] = win[size_t(i)] * norm;
    for (int i = 0; i < bins; ++i) omg[size_t(i)] = float(2 * M_PI / double(N)) * float(i);
}

void Vocoder::start(int v, double dir) {
    std::fill(ringL.begin() + v * ring, ringL.begin() + (v + 1) * ring, 0.f);
    std::fill(ringR.begin() + v * ring, ringR.begin() + (v + 1) * ring, 0.f);
    ringAt[size_t(v)] = 0;
    hopLeft[size_t(v)] = 0;
    primed[size_t(v)] = 0;
    prevHit[size_t(v)] = 0;
    late[size_t(v)] = 0;
    nextAt[size_t(v)] = INT64_MIN;
    lastDir[size_t(v)] = dir < 0 ? -1 : 1;
}

void Vocoder::fill(int v, const float* l, const float* r, int frames, bool stereo, double pos0, double speed,
                   double ratio, int m, int skip, double lo, double hi, const double* ons, int nOns, int64_t now) {
    const size_t V = size_t(v);
    const int H = hop;
    const int half = size / 2;
    if (speed > 1e-9)
        lastDir[V] = 1;
    else if (speed < -1e-9)
        lastDir[V] = -1;
    const double step = lastDir[V] * ratio;
    int j = 0;
    while (j < skip && j < m) {
        outL[size_t(j)] = 0;
        outR[size_t(j)] = 0;
        ++j;
    }
    float* rl = ringL.data() + v * ring;
    float* rr = ringR.data() + v * ring;
    double t = pos0;
    while (j < m) {
        // Новая нота или указатель вышел из своего куска (дошёл до атаки) — начать заново с этого места.
        if (primed[V] == 0 || t < segLo[V] || t >= segHi[V]) {
            segment(v, t, lo, hi, ons, nOns);
            first(v, l, r, frames, stereo, t, step, segLo[V], segHi[V]);
            primed[V] = 1;
            late[V] = 0;
            lead[V] = pickLead(v, now + int64_t(j));
            hopLeft[V] = H - lead[V];
        }
        const int a = ringAt[V];
        outL[size_t(j)] = rl[a];
        outR[size_t(j)] = stereo ? rr[a] : rl[a];
        rl[a] = 0;
        rr[a] = 0;
        ringAt[V] = (a + 1) & ringMask;
        t += speed;
        hopLeft[V] -= 1;
        if (hopLeft[V] <= 0) {
            const int d = late[V] != 0 ? 0 : lead[V];
            const double due = t + speed * double(d);
            if (d > 0 && (due < segLo[V] || due >= segHi[V])) {
                late[V] = 1;
                hopLeft[V] = d;
            } else {
                frame(v, l, r, frames, stereo, due + speed * double(half), step, ratio, segLo[V], segHi[V], d);
                hopLeft[V] = late[V] != 0 ? H - lead[V] : H;
                late[V] = 0;
            }
        }
        ++j;
    }
    nextAt[V] = now + int64_t(m + hopLeft[V] - 1);
}

int Vocoder::pickLead(int v, int64_t s) {
    const int H = hop;
    const int blk = std::max(1, block);
    const int groups = H / blk;
    if (groups < 2) return 0;
    const int64_t b64 = blk;
    const int64_t g64 = groups;
    int best = 0;
    int bestN = INT_MAX;
    for (int c = 0; c < groups; ++c) {
        const int d = c * blk;
        const int64_t at = s + int64_t(H - d - 1);
        const int64_t g = ((at / b64) % g64 + g64) % g64;
        int cnt = 0;
        for (int u = 0; u < slots; ++u) {
            const int64_t x = nextAt[size_t(u)];
            if (u != v && x >= s && ((x / b64) % g64 + g64) % g64 == g) ++cnt;
        }
        if (cnt < bestN) {
            bestN = cnt;
            best = d;
        }
    }
    return best;
}

void Vocoder::segment(int v, double t, double lo, double hi, const double* o, int n) {
    double a = lo, b = hi;
    if (o && n > 0) {
        int i = 0, k = n;
        while (i < k) {
            const int mid = (i + k) / 2;
            if (o[mid] <= t)
                i = mid + 1;
            else
                k = mid;
        }
        if (i > 0) a = std::max(lo, o[i - 1]);
        if (i < n) b = std::min(hi, o[i]);
    }
    if (t < a) a = t;
    if (t >= b) b = t + 1;
    segLo[size_t(v)] = a;
    segHi[size_t(v)] = b;
}

void Vocoder::read(const float* p, int frames, double c, double step, double lo, double hi, float* out, bool raw) {
    const int N = size;
    const double x0 = c - double(N / 2) * step;
    if (cub.read(p, frames, x0, step, N, lo, hi, out)) {
        if (!raw) pvMul(out, win.data(), out, N);
        return;
    }
    const float* w = win.data();
    for (int i = 0; i < N; ++i) {
        const double x = x0 + double(i) * step;
        float y = 0;
        if (x >= lo && x < hi && x >= 0) {
            const int k = int(x);
            const float f = float(x - double(k));
            const float ym1 = k >= 1 && k - 1 < frames ? p[k - 1] : 0;
            const float y0 = k < frames ? p[k] : 0;
            const float y1 = k + 1 < frames ? p[k + 1] : 0;
            const float y2 = k + 2 < frames ? p[k + 2] : 0;
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2 * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            y = ((c3 * f + c2) * f + c1) * f + y0;
        }
        out[i] = raw ? y : y * w[i];
    }
}

void Vocoder::analyze(bool stereo) {
    const int B = bins;
    if (stereo) {
        pvAdd(reL.data(), reR.data(), reM.data(), B);
        pvAdd(imL.data(), imR.data(), imM.data(), B);
    } else {
        std::copy(reL.begin(), reL.end(), reM.begin());
        std::copy(imL.begin(), imL.end(), imM.begin());
    }
    for (size_t k = 0; k < size_t(B); ++k) {
        ph[k] = std::atan2(imM[k], reM[k]);
        mag[k] = std::sqrt(reM[k] * reM[k] + imM[k] * imM[k]);
    }
}

void Vocoder::first(int v, const float* l, const float* r, int frames, bool stereo, double t0, double step, double lo,
                    double hi) {
    const int N = size;
    const int B = bins;
    const double c0 = t0 + double(N / 2) * step;
    read(l, frames, c0, step, lo, hi, fL.data(), true);
    if (stereo) read(r, frames, c0, step, lo, hi, fR.data(), true);
    const int a = ringAt[size_t(v)];
    pvRingMa(ringL.data() + v * ring, ring, a, fL.data(), firstW.data(), N);
    pvMul(fL.data(), win.data(), fL.data(), N);
    if (stereo) {
        pvRingMa(ringR.data() + v * ring, ring, a, fR.data(), firstW.data(), N);
        pvMul(fR.data(), win.data(), fR.data(), N);
    }
    fft.forward(fL.data(), reL.data(), imL.data());
    if (stereo) fft.forward(fR.data(), reR.data(), imR.data());
    analyze(stereo);
    const size_t b = size_t(v * B);
    std::copy(ph.begin(), ph.end(), prevAna.begin() + long(b));
    std::copy(ph.begin(), ph.end(), prevSyn.begin() + long(b));
    std::copy(mag.begin(), mag.end(), prevMag.begin() + long(b));
    std::copy(omg.begin(), omg.end(), instF.begin() + long(b));
    prevC[size_t(v)] = c0;
    prevHit[size_t(v)] = 1;
}

void Vocoder::frame(int v, const float* l, const float* r, int frames, bool stereo, double c, double step,
                    double ratio, double lo, double hi, int d) {
    const int N = size;
    const int B = bins;
    const float H = float(hop);
    read(l, frames, c, step, lo, hi, fL.data(), false);
    if (stereo) read(r, frames, c, step, lo, hi, fR.data(), false);
    fft.forward(fL.data(), reL.data(), imL.data());
    if (stereo) fft.forward(fR.data(), reR.data(), imR.data());
    analyze(stereo);
    const size_t b = size_t(v * B);
    float* pa = prevAna.data() + b;
    float* psy = prevSyn.data() + b;
    float* pm = prevMag.data() + b;
    float* fi = instF.data() + b;
    const float* MG = mag.data();
    const float* PH = ph.data();
    const float* OM = omg.data();
    // Шаг анализа в отсчётах прочитанного кадра (со скоростью высоты).
    const float ha = float(std::abs(c - prevC[size_t(v)]) / std::max(1e-6, ratio));
    const float twoPi = 6.2831855f;
    const float inv2Pi = 0.15915494f;
    const float floor_ = pvMax(MG, B) * 1e-3f + 1e-9f;
    int rise = 0, live = 0, np = 0;
    for (int k = 2; k < B - 2; ++k) {
        const float x = MG[k];
        if (x > floor_) {
            if (k >= 8) {
                ++live;
                if (x > 2 * pm[k]) ++rise;
            }
            if (x > MG[k - 1] && x > MG[k - 2] && x >= MG[k + 1] && x >= MG[k + 2]) {
                peaks[size_t(np)] = k;
                ++np;
            }
        }
    }
    const bool hit = live > 16 && float(rise) >= 0.3f * float(live);
    const bool reset = hit && prevHit[size_t(v)] == 0;
    prevHit[size_t(v)] = hit ? 1 : 0;
    const float iha = ha > 1 ? 1 / ha : 0;
    if (ha > 1 && !reset) {
        if (np == 0) {
            float* DC = dc.data();
            pvSub(PH, pa, DC, B);
            pvSma(OM, -ha, DC, DC, B);
            pvWrap(DC, cub.ki.data(), cub.kf.data(), B);
            pvSma(DC, iha, OM, fi, B);
        } else {
            for (int p = 0; p < np; ++p) {
                const int q0 = peaks[size_t(p)];
                const float om = OM[q0];
                float dd = PH[q0] - pa[q0] - om * ha;
                const float q = dd * inv2Pi;
                dd -= twoPi * float(int64_t(q >= 0 ? q + 0.5f : q - 0.5f));
                fi[q0] = om + dd * iha;
            }
        }
    }
    if (reset) {
        std::copy(PH, PH + B, psy);
    } else if (np == 0) {
        pvSma(fi, H, psy, psy, B);
    } else {
        int s0 = 0;
        for (int p = 0; p < np; ++p) {
            const int q0 = peaks[size_t(p)];
            const int end = p + 1 < np ? (q0 + peaks[size_t(p + 1)]) / 2 + 1 : B;
            const float base = psy[q0] + fi[q0] * H - PH[q0];
            pvSAdd(PH + s0, base, psy + s0, end - s0);
            s0 = end;
        }
    }
    pvWrap(psy, cub.ki.data(), cub.kf.data(), B);
    pvSub(psy, PH, ds.data(), B);
    std::copy(PH, PH + B, pa);
    std::copy(MG, MG + B, pm);
    for (size_t k = 0; k < size_t(B); ++k) {
        const float a = ds[k];
        syn[k] = std::sin(a);
        dc[k] = std::cos(a);
    }
    pvRotate(reL.data(), imL.data(), dc.data(), syn.data(), B);
    if (stereo) pvRotate(reR.data(), imR.data(), dc.data(), syn.data(), B);
    fft.inverse(reL.data(), imL.data(), fL.data());
    if (stereo) fft.inverse(reR.data(), imR.data(), fR.data());
    const int a0 = ringAt[size_t(v)] + d;
    pvRingMa(ringL.data() + v * ring, ring, a0, fL.data(), winG.data(), N);
    if (stereo) pvRingMa(ringR.data() + v * ring, ring, a0, fR.data(), winG.data(), N);
    prevC[size_t(v)] = c;
    ++framesDone_;
}

}  // namespace jambo
