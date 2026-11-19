// Порт Core/Synth/LadderFilter.swift (ветка swift JAMbo).
#include "LadderFilter.h"
#include <algorithm>
#include <cmath>

namespace jambo {

double LadderFilter::gain(double hz, double sr) {
    const double f = std::min(hz, sr * 0.45);
    const double g = std::tan(M_PI * f / sr);
    return g / (1 + g);
}

void LadderFilter::run(int v, float* buf, int m, double hz0, double hz1, double qDb, double grit, double sr, float fq,
                       float dfq) {
    const double reso = std::min(1.0, std::max(0.0, (qDb - 0.7) / 18));
    const double k = 3.9 * reso;
    const double gd = std::pow(10.0, 1.2 * std::min(1.0, std::max(0.0, grit)));
    const double norm = std::tanh(0.58) / std::tanh(0.58 * gd) * std::pow(gd, -0.2) * (1 + 0.5 * k);
    double gc = gain(hz0, sr);
    const double g1 = gain(hz1, sr);
    const double dG = (g1 - gc) / double(std::max(1, m));
    const size_t b = size_t(v) * 4;
    double s1 = st[b], s2 = st[b + 1], s3 = st[b + 2], s4 = st[b + 3];
    double w = double(fq);
    const double dw = double(dfq);
    for (int j = 0; j < m; ++j) {
        gc += dG;
        const double g = gc;
        const double be = 1 - g;
        const double g2 = g * g;
        const double g3 = g2 * g;
        const double g4 = g3 * g;
        const double dry = double(buf[j]);
        const double xin = dry * gd;
        // Выход четвёртого звена при линейной обратной связи — по нему вход с насыщением.
        const double sum = g3 * be * s1 + g2 * be * s2 + g * be * s3 + be * s4;
        const double y4 = (g4 * xin + sum) / (1 + k * g4);
        double u = std::tanh(xin - k * y4);
        double vv = (u - s1) * g;
        u = vv + s1;
        s1 = u + vv;
        vv = (u - s2) * g;
        u = vv + s2;
        s2 = u + vv;
        vv = (u - s3) * g;
        u = vv + s3;
        s3 = u + vv;
        vv = (u - s4) * g;
        u = vv + s4;
        s4 = u + vv;
        w += dw;
        buf[j] = float(dry + (u * norm - dry) * w);
    }
    // Денормалы на затухании — в ноль.
    if (std::fabs(s1) < 1e-20) s1 = 0;
    if (std::fabs(s2) < 1e-20) s2 = 0;
    if (std::fabs(s3) < 1e-20) s3 = 0;
    if (std::fabs(s4) < 1e-20) s4 = 0;
    st[b] = s1;
    st[b + 1] = s2;
    st[b + 2] = s3;
    st[b + 3] = s4;
}

}  // namespace jambo
