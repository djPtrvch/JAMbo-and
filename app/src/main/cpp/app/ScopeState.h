// Скоп мастера вне колбэка (порт ScopeState из MasterScopeView.swift, ветка swift JAMbo): осциллоскоп — по столбцу на
// точку ширины, в столбце минимум и максимум своего отрезка (окно 1 с), по кругу; гониометр — последние 4096 отсчётов
// выхода мастера; корреляция L и R.
#pragma once
#include <array>
#include <optional>
#include <vector>
#include "master/MasterScope.h"

namespace jambo {

class ScopeState {
public:
    static constexpr double window = 1;
    static constexpr int gonioN = 4096;
    /// Ширина осциллоскопа в точках (ставит экран).
    int width = 200;
    std::array<std::vector<float>, 4> mins, maxs;
    std::array<int, 4> pos{};
    std::vector<float> gl = std::vector<float>(gonioN, 0.f), gr = std::vector<float>(gonioN, 0.f);
    bool gonioOk = false;
    /// Корреляция за окно гониометра (−1…+1, сглажена); nullopt — тишина.
    std::optional<float> corr;

    ScopeState();
    void tick(const MasterScope& s, const std::array<bool, 4>& on);

private:
    std::array<int, 4> carry{};
    std::array<float, 4> cMin{}, cMax{};
    std::array<long, 4> seen{-1, -1, -1, -1};
    std::vector<float> buf;
    std::optional<float> correlation() const;
    void fold(int k, int n, int spc, int w);
    void clear(int k);
};

}  // namespace jambo
