// Ступени биквада EqUnit (порт Core/EQ/EqBiquads.swift, ветка swift JAMbo; Direct Form I): с постоянными
// коэффициентами, с линейно меняющимися и с коэффициентами на каждый отсчёт. x — Float, y и состояние — Double.
// Общие константы EqUnit и детекторов.
#pragma once

namespace jambo {

namespace EqDsp {
constexpr int maxFrames = 4096;
constexpr int quantum = 128;
constexpr int sub = 32;
/// Порог «тишины» состояния (≈ −180 dB).
constexpr double quiet = 1e-9;

/// Одна ступень.
inline void biquad(float* x, int n, const double* c, double* z) {
    const double b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
    double x1 = z[0], x2 = z[1], y1 = z[2], y2 = z[3];
    for (int i = 0; i < n; ++i) {
        const double xi = double(x[i]);
        const double y = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x[i] = float(y);
        x2 = x1;
        x1 = xi;
        y2 = y1;
        y1 = y;
    }
    z[0] = x1;
    z[1] = x2;
    z[2] = y1;
    z[3] = y2;
}

/// Ступень с коэффициентами, которые меняются линейно: в отсчёте k — c + dc·k.
inline void biquadRamp(float* x, int n, const double* c, const double* dc, double* z) {
    double b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
    const double d0 = dc[0], d1 = dc[1], d2 = dc[2], d3 = dc[3], d4 = dc[4];
    double x1 = z[0], x2 = z[1], y1 = z[2], y2 = z[3];
    for (int i = 0; i < n; ++i) {
        const double xi = double(x[i]);
        const double y = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x[i] = float(y);
        x2 = x1;
        x1 = xi;
        y2 = y1;
        y1 = y;
        b0 += d0;
        b1 += d1;
        b2 += d2;
        a1 += d3;
        a2 += d4;
    }
    z[0] = x1;
    z[1] = x2;
    z[2] = y1;
    z[3] = y2;
}

/// Ступень с коэффициентами на каждый отсчёт: в отсчёте k — c[5k…5k+4].
inline void biquadVar(float* x, int n, const double* c, double* z) {
    double x1 = z[0], x2 = z[1], y1 = z[2], y2 = z[3];
    int o = 0;
    for (int i = 0; i < n; ++i) {
        const double xi = double(x[i]);
        const double y = c[o] * xi + c[o + 1] * x1 + c[o + 2] * x2 - c[o + 3] * y1 - c[o + 4] * y2;
        x[i] = float(y);
        x2 = x1;
        x1 = xi;
        y2 = y1;
        y1 = y;
        o += 5;
    }
    z[0] = x1;
    z[1] = x2;
    z[2] = y1;
    z[3] = y2;
}
}  // namespace EqDsp

}  // namespace jambo
