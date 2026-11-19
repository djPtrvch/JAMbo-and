// Скоп мастера (порт Core/Master/MasterScope.swift, ветка swift JAMbo): точки съёма осциллоскопа — после сатуратора,
// компрессора, клиппера и лимитера (моно (L + R)/2) и конечный выход стерео для гониометра. Колбэк пишет только
// включённые точки (маска — от интерфейса).
#pragma once
#include "eq/Spectrum.h"

namespace jambo {

class MasterScope {
public:
    static constexpr int sat = 0, comp = 1, clip = 2, lim = 3;
    static constexpr int gonioBit = 1 << 4;
    SpectrumTap taps[4];
    SpectrumTap gonioL, gonioR;
    /// Биты 0…3 — точки осциллоскопа, gonioBit — гониометр; 0 — скоп выключен.
    AtomicCounter mask;

    void tap(int k, long m, const float* l, const float* r, int n) {
        if ((m & (1L << k)) == 0) return;
        taps[k].push(l, r, n);
    }
    void gonio(const float* l, const float* r, int n) {
        if ((mask.load() & gonioBit) == 0) return;
        gonioL.push(l, n);
        gonioR.push(r, n);
    }
};

}  // namespace jambo
