// Полоса MID или SIDE в EqUnit (порт Core/EQ/EqUnitMs.swift): вход · 0,5, M = l + r, S = l − r; фильтруется свой канал;
// обратно L = M + S, R = M − S (всё в Float). Фильтр полосы — моно: один отрезок траектории на квант. Особенность
// эталонов: в первом кванте новой цепочки SIDE получает тишину.
#include <algorithm>
#include <cmath>
#include "EqCoeffs.h"
#include "EqUnit.h"

namespace jambo {

void EqUnit::runMsBand(int c, int i, int n, bool old) {
    const size_t I = size_t(i);
    const int stages = nst[I];
    const bool side = msm[I] == 2;
    const int k = i - c * bands;
    double* cp = co.data() + I * 5;
    const size_t laneStride = size_t(2 * bands * bandState);
    double* s0 = st.data() + size_t(c * bands + k) * bandState;
    const int mf = maxFrames;
    float* l;
    float* r;
    // L/R → M/S.
    for (int lane = 0; lane < lanes; ++lane) {
        if (!signal(lane, old, l, r)) continue;
        float* m = midBuf.data() + lane * mf;
        float* sd = sideBuf.data() + lane * mf;
        for (int j = 0; j < n; ++j) {
            const float hl = l[j] * 0.5f;
            const float hr = r[j] * 0.5f;
            m[j] = hl + hr;
            sd[j] = hl + (-hr);
        }
    }
    if (side && !old && fadePos == 0) {
        const int q = std::min(quantum, n);
        for (int lane = 0; lane < lanes; ++lane)
            if (live[size_t(lane)] != 0) std::fill(sideBuf.begin() + lane * mf, sideBuf.begin() + lane * mf + q, 0.f);
    }
    float* buf = side ? sideBuf.data() : midBuf.data();
    const bool dyn = det.src[I] >= 0 && det.live[I] != 0;
    const float* off = det.off.data() + I * maxFrames;
    int a = 0;
    while (a < n) {
        const int w0 = std::min(quantum, n - a);
        const bool dq = dyn && nonZero(off + a, w0);
        if (moving[I] == 0 && !dq) {
            if (det.dirty[I] != 0) {
                det.dirty[I] = 0;
                EqCoeffs::coeffs(kind[I], tf[I], tg[I], tq[I], sr, cp);
            }
            for (int lane = 0; lane < lanes; ++lane) {
                if (live[size_t(lane)] == 0) continue;
                float* x = buf + lane * mf + a;
                double* z = s0 + size_t(lane) * laneStride;
                for (int s = 0; s < stages; ++s) EqDsp::biquad(x, w0, cp, z + s * 8);
            }
            a += w0;
            continue;
        }
        // Моно-узел: один отрезок траектории на квант.
        const bool mov = moving[I] != 0;
        const int e0 = el[I];
        if (mov) {
            el[I] += quantum;
            fresh[I] = 0;
        }
        if (dq) {
            det.dirty[I] = 1;
            fillCoeffs(i, mov, e0, off + a, w0, dynL.data());
            for (int lane = 0; lane < lanes; ++lane) {
                if (live[size_t(lane)] == 0) continue;
                float* x = buf + lane * mf + a;
                double* z = s0 + size_t(lane) * laneStride;
                for (int s = 0; s < stages; ++s) EqDsp::biquadVar(x, w0, dynL.data(), z + s * 8);
            }
        } else {
            double* dl = ramp;
            coeffsAt(i, double(e0), cp);
            int j = 0;
            while (j < w0) {
                const int w = std::min(sub, w0 - j);
                coeffsAt(i, double(e0 + j + w), dl);
                const double inv = 1 / double(w);
                for (int t = 0; t < 5; ++t) dl[t] = (dl[t] - cp[t]) * inv;
                for (int lane = 0; lane < lanes; ++lane) {
                    if (live[size_t(lane)] == 0) continue;
                    float* x = buf + lane * mf + a + j;
                    double* z = s0 + size_t(lane) * laneStride;
                    for (int s = 0; s < stages; ++s) EqDsp::biquadRamp(x, w, cp, dl, z + s * 8);
                }
                for (int t = 0; t < 5; ++t) cp[t] += dl[t] * double(w);
                j += w;
            }
        }
        if (mov && double(el[I]) * decay < -16.2) {
            moving[I] = 0;
            EqCoeffs::coeffs(kind[I], tf[I], tg[I], tq[I], sr, cp);
        }
        a += w0;
    }
    // M/S → L/R.
    for (int lane = 0; lane < lanes; ++lane) {
        if (!signal(lane, old, l, r)) continue;
        const float* m = midBuf.data() + lane * mf;
        const float* sd = sideBuf.data() + lane * mf;
        for (int j = 0; j < n; ++j) {
            l[j] = m[j] + sd[j];
            r[j] = m[j] + (-sd[j]);
        }
    }
}

}  // namespace jambo
