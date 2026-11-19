// Векторные операции таймстретча (порт Core/Sampler/PvVec.swift, ветка swift JAMbo): ветка без Accelerate — обычные
// циклы (по ним идут и тесты Swift-версии на Linux). Без выделений памяти; n > 0.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace jambo {

/// o = a · b.
inline void pvMul(const float* a, const float* b, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] * b[i];
}
/// o = a · b + c.
inline void pvMa(const float* a, const float* b, const float* c, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] * b[i] + c[i];
}
/// o = a · s + c.
inline void pvSma(const float* a, float s, const float* c, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] * s + c[i];
}
/// o = a · s + b · t.
inline void pvSmsma(const float* a, float s, const float* b, float t, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] * s + b[i] * t;
}
/// o = a + b.
inline void pvAdd(const float* a, const float* b, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] + b[i];
}
/// o = a − b.
inline void pvSub(const float* a, const float* b, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] - b[i];
}
/// o = a + s.
inline void pvSAdd(const float* a, float s, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] + s;
}
/// o = a · s.
inline void pvScale(const float* a, float s, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = a[i] * s;
}
/// Наибольшее значение.
inline float pvMax(const float* a, int n) {
    float m = a[0];
    for (int i = 1; i < n; ++i)
        if (a[i] > m) m = a[i];
    return m;
}
/// Углы x — к −π…π (вычесть ближайшее кратное 2π); qi, qf — рабочие буферы (в ветке без Accelerate не нужны).
inline void pvWrap(float* x, int32_t* /*qi*/, float* /*qf*/, int n) {
    const float twoPi = 6.2831855f;
    const float inv2Pi = 0.15915494f;
    for (int i = 0; i < n; ++i) {
        const float q = x[i] * inv2Pi;
        x[i] -= twoPi * float(int64_t(q >= 0 ? q + 0.5f : q - 0.5f));
    }
}
/// Поворот спектра на месте: (re + i·im) · (c + i·s).
inline void pvRotate(float* re, float* im, const float* c, const float* s, int n) {
    for (int i = 0; i < n; ++i) {
        const float x = re[i], y = im[i], cs = c[i], sn = s[i];
        re[i] = x * cs - y * sn;
        im[i] = x * sn + y * cs;
    }
}
/// Кольцо (размер size, степень двойки) с позиции at: ring += x · w (n ≤ size).
inline void pvRingMa(float* ring, int size, int at, const float* x, const float* w, int n) {
    const int a = at & (size - 1);
    const int first = std::min(n, size - a);
    pvMa(x, w, ring + a, ring + a, first);
    if (n > first) pvMa(x + first, w + first, ring, ring, n - first);
}
/// Позиции start + i·step: целая часть (Int32 и Float) и дробная (Float).
inline void pvSplitRamp(double start, double step, double* /*xd*/, int32_t* ki, float* kf, float* fr, int n) {
    for (int i = 0; i < n; ++i) {
        const double x = start + double(i) * step;
        const int32_t k = int32_t(x);
        ki[i] = k;
        kf[i] = float(k);
        fr[i] = float(x - double(k));
    }
}
/// o[i] = p[k[i]] (индексы — целые ≥ 0 в Float).
inline void pvGather(const float* p, const float* kf, float* o, int n) {
    for (int i = 0; i < n; ++i) o[i] = p[int(kf[i])];
}
/// Σ a · b.
inline float pvDot(const float* a, const float* b, int n) {
    float r = 0;
    for (int i = 0; i < n; ++i) r += a[i] * b[i];
    return r;
}
/// Σ a².
inline float pvSumSq(const float* a, int n) {
    float r = 0;
    for (int i = 0; i < n; ++i) r += a[i] * a[i];
    return r;
}

/// Кубическое чтение сэмпла (Catmull-Rom, как Sampler::cubic) векторно: n отсчётов в позициях x0 + i·step. Буферы — на
/// cap отсчётов, создаются вне колбэка; ki и kf свободны между чтениями (рабочие буферы для вызывающего).
class PvCubic {
public:
    explicit PvCubic(int n)
        : cap(n), xd(size_t(n)), ki(size_t(n)), kf(size_t(n)), fr(size_t(n)), g0(size_t(n)), g1(size_t(n)),
          g2(size_t(n)), g3(size_t(n)), t1(size_t(n)) {}

    const int cap;
    std::vector<double> xd;
    std::vector<int32_t> ki;
    std::vector<float> kf, fr, g0, g1, g2, g3, t1;

    /// Все позиции в lo…hi и внутри сэмпла (1 ≤ x < frames − 3) — out и true; иначе false (читать по отсчётам).
    bool read(const float* p, int frames, double x0, double step, int n, double lo, double hi, float* out) {
        const double x1 = x0 + double(n - 1) * step;
        const double xmin = std::min(x0, x1);
        if (n > cap || xmin < std::max(lo, 1.0) || std::max(x0, x1) >= std::min(hi, double(frames - 3))) return false;
        const int base = int(xmin);
        float* K = kf.data();
        float* F = fr.data();
        float* G0 = g0.data();
        float* G1 = g1.data();
        float* G2 = g2.data();
        float* G3 = g3.data();
        float* T1 = t1.data();
        pvSplitRamp(x0 - double(base), step, xd.data(), ki.data(), K, F, n);
        const float* q = p + (base - 1);
        pvGather(q, K, G0, n);
        pvGather(q + 1, K, G1, n);
        pvGather(q + 2, K, G2, n);
        pvGather(q + 3, K, G3, n);
        pvSub(G3, G0, T1, n);
        pvSub(G1, G2, K, n);
        pvSmsma(T1, 0.5f, K, 1.5f, T1, n);
        pvSmsma(G0, 1, G3, -0.5f, K, n);
        pvSmsma(G2, 2, G1, -2.5f, G3, n);
        pvAdd(K, G3, K, n);
        pvSmsma(G2, 0.5f, G0, -0.5f, out, n);
        pvMa(T1, F, K, T1, n);
        pvMa(T1, F, out, T1, n);
        pvMa(T1, F, G1, out, n);
        return true;
    }
};

}  // namespace jambo
