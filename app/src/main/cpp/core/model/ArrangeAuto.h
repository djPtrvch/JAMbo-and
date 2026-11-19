// Автоматизация на таймлайне: точки и кривые (порт ArrAutoPoint, ArrAutoLane, Core/Model/ArrangeAuto.swift, ветка
// swift JAMbo; цели и правки — с аранжировщиком).
#pragma once
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include "Eqs.h"

namespace jambo {

/// Точка кривой: x — шаг таймлайна, y — положение ручки 0…1.
struct ArrAutoPoint {
    double x = 0, y = 0;
    auto tie() const { return std::tie(x, y); }
    JAMBO_EQ(ArrAutoPoint)
};

/// Кривая ручки param — точки по x.
struct ArrAutoLane {
    static constexpr int maxPoints = 2048;
    std::string param;
    std::vector<ArrAutoPoint> points;

    /// Значение 0…1 на шаге x; nullopt — до первой точки, после последней или точек нет.
    std::optional<double> value(double x) const { return value(points, x); }

    static std::optional<double> value(const std::vector<ArrAutoPoint>& p, double x) {
        if (p.empty() || x < p.front().x || x > p.back().x) return std::nullopt;
        int lo = 0, hi = int(p.size()) - 1;
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            if (p[size_t(mid)].x <= x) lo = mid; else hi = mid;
        }
        const ArrAutoPoint& a = p[size_t(lo)];
        const ArrAutoPoint& b = p[size_t(hi)];
        if (b.x <= a.x) return b.y;
        const double f = (x - a.x) / (b.x - a.x);
        return a.y + (b.y - a.y) * std::min(1.0, std::max(0.0, f));
    }
    auto tie() const { return std::tie(param, points); }
    JAMBO_EQ(ArrAutoLane)
};

}  // namespace jambo
