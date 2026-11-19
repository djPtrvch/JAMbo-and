// WARP: метки варпинга сэмпла и карта времени (порт WarpMark, WarpInfo, WarpMap, Core/Model/Warp.swift, ветка swift
// JAMbo).
#pragma once
#include <algorithm>
#include <cmath>
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

/// Карта времени сэмпла: метки (отсчёт и доля, по возрастанию обоих) и отсчётов на долю в темпе файла.
struct WarpMap {
    std::vector<double> pos, beats;
    double perBeat;

    /// Самые узкие допуски: между метками не меньше 1/64 доли и 1 мс.
    static constexpr double minBeat = 1.0 / 64, minFrames = 48, bpmMin = 20, bpmMax = 400;
    /// BPM файла, если не определился и не задан.
    static constexpr double fallbackBpm = 120;

    WarpMap(const WarpInfo& info, int frames, double bpm) : perBeat(48000.0 * 60 / std::max(1.0, bpm)) {
        for (const auto& m : info.marks) {
            pos.push_back(m.at * double(frames));
            beats.push_back(m.beat);
        }
    }

    /// BPM файла: темп проекта follow (WARP TO PROJECT BPM), иначе свой, иначе определённый сам, иначе 120.
    static double bpm(const WarpInfo* info, double detected, std::optional<double> follow = std::nullopt) {
        if (follow && info && !info->marks.empty() && !info->isOff()) return *follow;
        if (info && info->bpm) return *info->bpm;
        return detected > 0 ? detected : fallbackBpm;
    }

    /// Доля отсчёта p.
    double beat(double p) const {
        const size_t n = pos.size();
        if (n == 0) return p / perBeat;
        if (p <= pos[0]) return beats[0] - (pos[0] - p) / perBeat;
        if (p >= pos[n - 1]) return beats[n - 1] + (p - pos[n - 1]) / perBeat;
        size_t k = 1;
        while (pos[k] < p) ++k;
        const double f = (p - pos[k - 1]) / std::max(1e-9, pos[k] - pos[k - 1]);
        return beats[k - 1] + f * (beats[k] - beats[k - 1]);
    }

    /// Отсчёт доли b.
    double position(double b) const {
        const size_t n = beats.size();
        if (n == 0) return b * perBeat;
        if (b <= beats[0]) return pos[0] - (beats[0] - b) * perBeat;
        if (b >= beats[n - 1]) return pos[n - 1] + (b - beats[n - 1]) * perBeat;
        size_t k = 1;
        while (beats[k] < b) ++k;
        const double f = (b - beats[k - 1]) / std::max(1e-9, beats[k] - beats[k - 1]);
        return pos[k - 1] + f * (pos[k] - pos[k - 1]);
    }

    /// AUTO WARP: метка начала — на долю 0, метки на атаки — на ближайшую долю сетки grid (в четвертях).
    static std::vector<WarpMark> autoMarks(const std::vector<double>& onsets, int frames, double bpm, double grid) {
        const double per = 48000.0 * 60 / std::max(1.0, bpm);
        const double g = std::max(1.0 / 64, grid);
        std::vector<WarpMark> out = {WarpMark{0, 0}};
        double last = 0;
        for (double p : onsets) {
            if (p < minFrames || p >= double(frames)) continue;
            const double b = std::round(p / per / g) * g;
            if (b <= last + minBeat / 2) continue;
            out.push_back(WarpMark{p / double(frames), b});
            last = b;
        }
        return out;
    }
};

}  // namespace jambo
