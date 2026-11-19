// Спектры EQ (порт Core/EQ/Spectrum.swift, ветка swift JAMbo): моно (L + R)/2, окно Блэкмана, FFT 4096, |X|/N,
// сглаживание 0,6 между чтениями, dB. Колбэк пишет сигнал в кольцевые буферы точек съёма (SpectrumTap), интерфейс читает
// последние 4096 отсчётов и считает спектр (SpectrumAnalyzer) — вне колбэка.
#pragma once
#include <vector>
#include "util/AtomicFloat.h"

namespace jambo {

/// Кольцевой буфер точки съёма: пишет колбэк, читает интерфейс.
class SpectrumTap {
public:
    static constexpr int capacity = 8192;
    static constexpr int mask = capacity - 1;
    SpectrumTap() : buf(capacity, 0.f) {}
    AtomicCounter written;

    /// Колбэк: n отсчётов стерео → моно (L + R)/2.
    void push(const float* l, const float* r, int n) {
        const long w = written.load();
        for (int i = 0; i < n; ++i) buf[size_t((w + i) & mask)] = 0.5f * (l[i] + r[i]);
        written.store(w + n);
    }
    /// Колбэк: n отсчётов одного канала.
    void push(const float* x, int n) {
        const long w = written.load();
        for (int i = 0; i < n; ++i) buf[size_t((w + i) & mask)] = x[i];
        written.store(w + n);
    }
    void pushSilence(int n) {
        const long w = written.load();
        for (int i = 0; i < n; ++i) buf[size_t((w + i) & mask)] = 0;
        written.store(w + n);
    }
    /// Интерфейс: count отсчётов, кончая отсчётом w.
    void read(float* out, int count, long w) const {
        for (int i = 0; i < count; ++i) out[i] = buf[size_t((w - count + i) & mask)];
    }
    void read(float* out, int count) const { read(out, count, written.load()); }

private:
    std::vector<float> buf;
};

/// Точки съёма спектров EQ и что они слушают (ставит интерфейс, читает колбэк).
class Spectra {
public:
    Spectra() {
        channel.store(-1);
        compareTrack.store(-1);
    }
    SpectrumTap pre, post, compare;
    /// Канал EQ на экране (−1 — EQ закрыт).
    AtomicCounter channel;
    /// Дорожка для сравнения (−1 — нет).
    AtomicCounter compareTrack;
};

/// БПФ вещественного сигнала (вне колбэка): бины 0…size/2−1.
class RealFFT {
public:
    explicit RealFFT(int size);
    void forward(const float* x, float* re, float* im);

private:
    int half, bits = 0;
    std::vector<float> cr, ci, wc, ws, sc, ss;
    std::vector<int> rev;
};

/// Спектр (FFT 4096, сглаживание между кадрами 0,6): 2048 бинов, dB.
class SpectrumAnalyzer {
public:
    static constexpr int size = 4096;
    static constexpr int bins = size / 2;
    static constexpr float smoothing = 0.6f;
    SpectrumAnalyzer();
    /// Спектр в dB (bins значений) после analyze.
    std::vector<float> db;
    void analyze(const SpectrumTap& tap);
    void analyze(const std::vector<float>& x);

private:
    std::vector<float> window, input, mag, re, im;
    RealFFT fft;
    void analyzeInput();
};

}  // namespace jambo
