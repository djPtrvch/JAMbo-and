// Свои движки JAMbo: константы (порт enum Jambo и JamboDrift из Core/Jambo/JamboTables.swift, ветка swift JAMbo).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace jambo {
namespace J {

constexpr int drum = 16, analog = 17, fm = 18, wtable = 19;
/// Ударные (S12, этап 3).
constexpr int snare = 20, hat = 21, clap = 22, perc = 23;
/// SUB: моно-бас.
constexpr int sub = 24;
constexpr double subOut = 0.58;
constexpr double subFat = 35.0;   // FAT: расстройка OSC2 до 35 центов
/// TEXTURE и GRANULAR: стерео-выход.
constexpr int texture = 25, granular = 26;

/// Монофоничный движок (слот 0): ударные (KICK, 20–23), SUB и TEXTURE.
inline bool mono(int e) { return e == drum || (e >= snare && e <= texture); }
/// Стерео-выход движка (TEXTURE, GRANULAR).
inline bool stereo(int e) { return e == texture || e == granular; }

// HAT: средний тон ряда 808, уровень выхода.
constexpr double hatMean = 416.4, hatOut = 1.4;
// CLAP: интервал вспышек, мс; уровень выхода.
constexpr double clapGapMin = 2.0, clapGapMax = 25.0, clapOut = 2.4;

/// TONE CLAP: 700 → 3000 Гц, кривая x^1.69.
inline double clapTone(double n) { return 700 * std::pow(3000.0 / 700, std::pow(std::min(1.0, std::max(0.0, n)), 1.69)); }
/// TUNE SNARE (120–400 Гц), PERC (60 Гц–2 кГц).
inline double snareTune(double n) { return 120 * std::pow(400.0 / 120, n); }
inline double percTune(double n) { return 60 * std::pow(2000.0 / 60, n); }

struct Mode { double r, a; };
/// Мода k набора PERC: 0 — мембрана, 1 — брусок, 2 — металл.
inline Mode percMode(int set, int k) {
    switch (set * 6 + k) {
    case 0: return {1, 1};
    case 1: return {1.59, 0.7};
    case 2: return {2.14, 0.5};
    case 3: return {2.30, 0.4};
    case 4: return {2.65, 0.3};
    case 5: return {2.92, 0.25};
    case 6: return {1, 1};
    case 7: return {2.76, 0.5};
    case 8: return {5.40, 0.3};
    case 9: return {8.93, 0.2};
    case 10: return {13.34, 0.1};
    case 11: return {18.64, 0.06};
    case 12: return {1, 1};
    case 13: return {1.4815, 0.9};
    case 14: return {3, 0.33};
    case 15: return {4.4444, 0.3};
    case 16: return {5, 0.2};
    default: return {7.4074, 0.18};
    }
}
/// Мода k PERC по MATERIAL m (между наборами — плавно, отношения — в логарифме) и уровень с учётом HIT.
inline Mode percModeAt(double m, double hit, int k) {
    const double x = std::min(1.0, std::max(0.0, m));
    const int set = x < 0.5 ? 0 : 1;
    const double u = x < 0.5 ? x * 2 : (x - 0.5) * 2;
    const Mode lo = percMode(set, k), hi = percMode(set + 1, k);
    const double r = std::exp(std::log(lo.r) + (std::log(hi.r) - std::log(lo.r)) * u);
    const double a = (lo.a + (hi.a - lo.a) * u) * std::pow(0.55 + 0.45 * hit, double(k));
    return {r, a};
}

/// Кусок голоса, кадров; плавный вход новой ноты (кроме DRUM); сглаживание ручек, с.
constexpr int block = 24;
constexpr int fadeIn = 64;
constexpr double smooth = 0.005;

// DRUM
constexpr double drumPitchMin = 30.0, drumPitchMax = 100.0, drumT60 = 6.9, drumDecayMax = 1000.0;
constexpr double drumDeclick = 0.002, drumClick = 0.002, drumSine = 0.7, drumOut = 0.85;

// ANALOG
constexpr double analogDetune = 60.0, analogDrift = 3.0, analogDriftRate = 2.0, analogOut = 0.62;
constexpr double analogWaveXf = 0.005, analogFmIndex = 1.0;

// WTABLE
constexpr double wtUnisonOld = 40.0, wtDetune = 60.0, wtSide = 0.708, wtOut = 0.5;

// FM
inline double fmSnap(int i) {
    static constexpr double v[10] = {0.5, 1, 1.5, 2, 3, 4, 5, 6, 7, 8};
    return v[std::min(9, std::max(0, i))];
}
constexpr double fmIndex = 10.0, fmFeedback = 0.35, fmOut = 0.7;

/// Полуполосный фильтр децимации ×2 (окно Блэкмана–Харриса), нечётная длина.
constexpr int hbLen = 63;
constexpr int sinN = 4096;
constexpr int waveSteps = 64;

/// RATIO движка FM по ручке 0…1: 0.5 → 8 логарифмически, с привязкой (±2 %).
inline double fmRatio(double n) {
    const double r = 0.5 * std::pow(16.0, std::min(1.0, std::max(0.0, n)));
    for (int i = 0; i < 10; ++i) {
        const double t = fmSnap(i);
        if (std::fabs(r / t - 1) <= 0.02) return t;
    }
    return r;
}

/// PITCH DRUM по ручке 0…1, Гц.
inline double drumPitch(double n) {
    return drumPitchMin * std::pow(drumPitchMax / drumPitchMin, std::min(1.0, std::max(0.0, n)));
}

/// Пункт WAVE (ручка 0…1, четыре пункта).
inline int waveIndex(double v) { return std::min(3, std::max(0, int(std::floor(v * 4)))); }

}  // namespace J

/// Дрейф ANALOG (общий на дорожку): центы, цель, множитель частоты, время дорожки с дрейфом и генератор xorshift32.
struct JamboDrift {
    double drift = 0, driftTo = 0, ratio = 1, tau = 0;
    int64_t rnd = 33333;

    static int64_t xorshift(int64_t r0) {
        const int64_t m = 0xFFFFFFFFLL;
        int64_t r = r0;
        r ^= (r << 13) & m;
        r ^= r >> 17;
        r ^= (r << 5) & m;
        return r;
    }

    /// Начало блока process() из frames кадров: новая цель дрейфа (в среднем раз в 0,5 с), плавный ход за 0,7 с.
    void begin(int frames, double sr) {
        rnd = xorshift(rnd);
        if (double(rnd) / 4294967296.0 < J::analogDriftRate * double(frames) / sr) {
            rnd = xorshift(rnd);
            driftTo = (double(rnd) / 4294967296.0 * 2 - 1) * J::analogDrift;
        }
        drift += (driftTo - drift) * double(frames) / (0.7 * sr);
        ratio = std::pow(2.0, drift / 1200);
    }

    /// Конец блока: время дорожки с дрейфом.
    void end(int frames) { tau += double(frames) * ratio; }
};

}  // namespace jambo
