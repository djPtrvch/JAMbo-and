// Динамические полосы в EqUnit (порт Core/EQ/EqUnitDyn.swift): детекторы цепочки и коэффициенты полосы на каждый
// отсчёт со сдвигом усиления; пока частота и Q не сглаживаются, синус и косинус — один раз на квант.
#include <algorithm>
#include <cmath>
#include "EqCoeffs.h"
#include "EqUnit.h"
#include "sampler/SoundSnapshot.h"

namespace jambo {

void EqUnit::runDetectors(int c, int n) {
    for (int k = 0; k < bands; ++k) {
        const int i = c * bands + k;
        const int s = det.src[size_t(i)];
        if (s < 0) continue;
        if (s == 0) {
            det.run(i, selfL.data(), selfR.data(), n, sr, decay);
        } else if (s <= SoundData::maxTracks && trackL && trackR) {
            det.run(i, trackL + (s - 1) * trackStride, trackR + (s - 1) * trackStride, n, sr, decay);
        } else {
            det.run(i, nullptr, nullptr, n, sr, decay);
        }
    }
}

bool EqUnit::nonZero(const float* x, int n) {
    for (int i = 0; i < n; ++i)
        if (x[i] != 0) return true;
    return false;
}

void EqUnit::coeffsAtDyn(int i, bool mov, double e, double off, double* out) {
    const size_t I = size_t(i);
    const double x = mov ? std::exp(e * decay) : 0;
    const double f = tf[I] + (sf[I] - tf[I]) * x;
    const double g = tg[I] + (sg[I] - tg[I]) * x + off;
    const double q = tq[I] + (sq[I] - tq[I]) * x;
    EqCoeffs::coeffs(kind[I], f, g, q, sr, out);
}

void EqUnit::fillCoeffs(int i, bool mov, int e0, const float* off, int m, double* out) {
    const size_t I = size_t(i);
    const int t = kind[I];
    const double f = std::max(0.0, std::min(1.0, tf[I] / (sr * 0.5)));
    const double q = tq[I];
    if (mov || f <= 0 || f >= 1 || q <= 0 || t > EqSpec::highShelf) {
        for (int j = 0; j < m; ++j) coeffsAtDyn(i, mov, double(e0 + j), double(off[j]), out + j * 5);
        return;
    }
    const double g0 = tg[I];
    const double w0 = M_PI * f;
    const double k = std::cos(w0);
    const double sn = std::sin(w0);
    const bool bell = t == EqSpec::bell;
    const bool low = t == EqSpec::lowShelf;
    const double alphaBell = sn / (2 * q);
    const double alphaShelf = 0.5 * sn * 1.4142135623730951;
    float prev = 0;
    for (int j = 0; j < m; ++j) {
        double* o = out + j * 5;
        const float v = off[j];
        if (j > 0 && v == prev) {
            for (int c = 0; c < 5; ++c) o[c] = o[c - 5];
            continue;
        }
        prev = v;
        const double A = std::pow(10.0, (g0 + double(v)) / 40);
        double b0, b1, b2, a0, a1, a2;
        if (bell) {
            b0 = 1 + alphaBell * A;
            b1 = -2 * k;
            b2 = 1 - alphaBell * A;
            a0 = 1 + alphaBell / A;
            a1 = -2 * k;
            a2 = 1 - alphaBell / A;
        } else {
            const double k2 = 2 * std::sqrt(A) * alphaShelf;
            const double ap = A + 1;
            const double am = A - 1;
            if (low) {
                b0 = A * (ap - am * k + k2);
                b1 = 2 * A * (am - ap * k);
                b2 = A * (ap - am * k - k2);
                a0 = ap + am * k + k2;
                a1 = -2 * (am + ap * k);
                a2 = ap + am * k - k2;
            } else {
                b0 = A * (ap + am * k + k2);
                b1 = -2 * A * (am + ap * k);
                b2 = A * (ap + am * k - k2);
                a0 = ap - am * k + k2;
                a1 = 2 * (am - ap * k);
                a2 = ap - am * k - k2;
            }
        }
        const double inv = 1 / a0;
        o[0] = b0 * inv;
        o[1] = b1 * inv;
        o[2] = b2 * inv;
        o[3] = a1 * inv;
        o[4] = a2 * inv;
    }
}

}  // namespace jambo
