// WARP: метки варпинга сэмпла (порт структур Core/Model/Warp.swift, ветка swift JAMbo; карта времени — со звуком).
#pragma once
#include <optional>
#include <vector>
#include "Eqs.h"

namespace jambo {

/// Метка: доля длины сэмпла 0…1 и её доля (четверти от начала сэмпла).
struct WarpMark {
    double at = 0;
    double beat = 0;
    auto tie() const { return std::tie(at, beat); }
    JAMBO_EQ(WarpMark)
};

/// Варпинг сэмпла: свой BPM файла (nullopt — определённый сам), метки, off — выключен.
struct WarpInfo {
    std::optional<double> bpm;
    std::vector<WarpMark> marks;
    std::optional<bool> off;

    bool isEmpty() const { return !bpm && marks.empty(); }
    bool isOff() const { return off == true; }
    auto tie() const { return std::tie(bpm, marks, off); }
    JAMBO_EQ(WarpInfo)
};

}  // namespace jambo
