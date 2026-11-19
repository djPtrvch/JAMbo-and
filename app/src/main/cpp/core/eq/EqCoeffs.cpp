// Коэффициенты биквада полосы EQ (порт Core/EQ/EqCoeffs.swift).
#include "EqCoeffs.h"
#include <algorithm>
#include <cmath>
#include "EqSnapshot.h"

namespace jambo {
namespace EqCoeffs {

void coeffs(int type, double hz, double db, double q, double sr, double* out) {
    const double f = std::max(0.0, std::min(1.0, hz / (sr * 0.5)));
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    const double w0 = M_PI * f;
    const double k = std::cos(w0);
    switch (type) {
    case EqSpec::bell: {
        const double A = std::pow(10.0, db / 40);
        if (f > 0 && f < 1) {
            if (q > 0) {
                const double alpha = std::sin(w0) / (2 * q);
                b0 = 1 + alpha * A;
                b1 = -2 * k;
                b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;
                a1 = -2 * k;
                a2 = 1 - alpha / A;
            } else {
                b0 = A * A;
            }
        }
        break;
    }
    case EqSpec::lowShelf:
    case EqSpec::highShelf: {
        const double A = std::pow(10.0, db / 40);
        const bool low = type == EqSpec::lowShelf;
        if (f >= 1) {
            b0 = low ? A * A : 1;
        } else if (f <= 0) {
            b0 = low ? 1 : A * A;
        } else {
            const double alpha = 0.5 * std::sin(w0) * 1.4142135623730951;
            const double k2 = 2 * std::sqrt(A) * alpha;
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
        break;
    }
    case EqSpec::lowCut:
    case EqSpec::highCut: {
        const bool high = type == EqSpec::lowCut;
        if (f >= 1) {
            b0 = high ? 0 : 1;
        } else if (f <= 0) {
            b0 = high ? 1 : 0;
        } else {
            const double g = std::pow(10.0, 0.05 * q);
            const double alpha = std::sin(w0) / (2 * g);
            const double beta = high ? (1 + k) / 2 : (1 - k) / 2;
            b0 = beta;
            b1 = high ? -2 * beta : 2 * beta;
            b2 = beta;
            a0 = 1 + alpha;
            a1 = -2 * k;
            a2 = 1 - alpha;
        }
        break;
    }
    case EqSpec::notch:
        if (f > 0 && f < 1 && q > 0) {
            const double alpha = std::sin(w0) / (2 * q);
            b0 = 1;
            b1 = -2 * k;
            b2 = 1;
            a0 = 1 + alpha;
            a1 = -2 * k;
            a2 = 1 - alpha;
        } else if (f > 0 && f < 1) {
            b0 = 0;
        }
        break;
    case bandpass:
        if (f > 0 && f < 1) {
            if (q > 0) {
                const double alpha = std::sin(w0) / (2 * q);
                b0 = alpha;
                b1 = 0;
                b2 = -alpha;
                a0 = 1 + alpha;
                a1 = -2 * k;
                a2 = 1 - alpha;
            }
        } else {
            b0 = 0;
        }
        break;
    default:
        break;
    }
    const double inv = 1 / a0;
    out[0] = b0 * inv;
    out[1] = b1 * inv;
    out[2] = b2 * inv;
    out[3] = a1 * inv;
    out[4] = a2 * inv;
}

std::vector<double> response(const EqBand& b, const std::vector<double>& hz, double sampleRate) {
    double c[5];
    coeffs(b.type, b.freq, EqSpec::hasGain(b.type) ? b.gain : 0, EqSpec::filterQ(b), sampleRate, c);
    const double n = double(EqSpec::stages(b));
    std::vector<double> out;
    out.reserve(hz.size());
    for (double f : hz) {
        const double w = 2 * M_PI * f / sampleRate;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = c[0] + c[1] * c1 + c[2] * c2, ni = -(c[1] * s1 + c[2] * s2);
        const double dr = 1 + c[3] * c1 + c[4] * c2, di = -(c[3] * s1 + c[4] * s2);
        const double mag = std::sqrt((nr * nr + ni * ni) / std::max(1e-30, dr * dr + di * di));
        out.push_back(n * 20 * std::log10(std::max(1e-6, mag)));
    }
    return out;
}

double response(const EqBand& b, double hz, double sampleRate) { return response(b, std::vector<double>{hz}, sampleRate)[0]; }

}  // namespace EqCoeffs
}  // namespace jambo
