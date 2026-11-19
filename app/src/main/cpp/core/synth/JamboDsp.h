// Общие помощники DSP своих движков (порт jFrac, jFloor, jSin, jBlep, jTriAt, jShapeAt, jHalf, jFlush из
// Core/Jambo/JamboVoice.swift). Встраиваются в горячие циклы.
#pragma once
#include <cmath>

namespace jambo {
namespace dsp {

/// floor(x) для |x| < 2⁶³ (через целое, как в Swift).
inline double floorI(double x) {
    const double f = double((long long)x);
    return f > x ? f - 1 : f;
}
/// frac(x) = x − floor(x).
inline double frac(double x) { return x - std::floor(x); }

/// Синус по фазе в периодах (таблица 4096 с линейной интерполяцией).
inline double sinT(const double* table, double ph) {
    double x = ph - floorI(ph);
    x *= 4096;
    const int i = int(x);
    const double a = table[i], b = table[i + 1];
    return a + (b - a) * (x - double(i));
}

/// PolyBLEP: поправка у разрыва (t — фаза 0…1, dt — шаг фазы).
inline double blep(double t0, double dt) {
    if (t0 < dt) {
        const double t = t0 / dt;
        return t + t - t * t - 1;
    }
    if (t0 > 1 - dt) {
        const double t = (t0 - 1) / dt;
        return t * t + t + t + 1;
    }
    return 0;
}

/// Треугольник ANALOG (пик на φ = 0.25).
inline double triAt(double ph) { return 1 - 4 * std::fabs(ph < 0.75 ? ph - 0.25 : ph - 1.25); }

/// Отсчёт формы k осциллятора с фазой a, шагом dt и треугольником tri (интеграл квадрата).
inline double shapeAt(const double* sine, int k, double a, double dt, double tri) {
    if (k == 0) return sinT(sine, a);
    if (k == 1) return tri;
    if (k == 2) return 1 - 2 * a + blep(a, dt);
    return (a < 0.5 ? 1 : -1) + blep(a, dt) - blep(a < 0.5 ? a + 0.5 : a - 0.5, dt);
}

/// Полуполосный фильтр децимации (63 отвода): x — центр окна, k — пары центр ∓ (2j + 1), kc — центр.
inline double half(const double* x, const double* k, double kc) {
    double a = k[0] * (x[-1] + x[1]);
    a += k[1] * (x[-3] + x[3]);
    a += k[2] * (x[-5] + x[5]);
    a += k[3] * (x[-7] + x[7]);
    a += k[4] * (x[-9] + x[9]);
    a += k[5] * (x[-11] + x[11]);
    a += k[6] * (x[-13] + x[13]);
    a += k[7] * (x[-15] + x[15]);
    a += k[8] * (x[-17] + x[17]);
    a += k[9] * (x[-19] + x[19]);
    a += k[10] * (x[-21] + x[21]);
    a += k[11] * (x[-23] + x[23]);
    a += k[12] * (x[-25] + x[25]);
    a += k[13] * (x[-27] + x[27]);
    a += k[14] * (x[-29] + x[29]);
    a += k[15] * (x[-31] + x[31]);
    return a + kc * x[0];
}

inline double flush(double x) { return (x > 1e-20 || x < -1e-20) ? x : 0; }

}  // namespace dsp
}  // namespace jambo
