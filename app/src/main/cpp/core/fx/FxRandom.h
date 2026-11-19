// Генератор случайных чисел эффектов — mulberry32 (порт Core/FX/FxRandom.swift):
// с тем же seed модуляция совпадает с эталонами по отсчётам.
#pragma once
#include <cstdint>

namespace jambo {

struct FxRandom {
    uint32_t s;
    explicit FxRandom(uint32_t seed) : s(seed) {}

    /// 0 ≤ x < 1.
    double next() {
        s += 0x6D2B79F5u;
        uint32_t t = s;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + ((t ^ (t >> 7)) * (t | 61u));
        return double(t ^ (t >> 14)) / 4294967296.0;
    }
};

}  // namespace jambo
