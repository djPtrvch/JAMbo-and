// Назначение источника модуляции на ручку (порт ModAssign, Core/Model/Project.swift, ветка swift JAMbo).
#pragma once
#include <optional>
#include "Eqs.h"

namespace jambo {

/// lfo 1–3 — LFO1–3, 4 — ENV; amount −1…+1 (доля хода ручки); track — у назначения на ручку FX: дорожка-источник.
struct ModAssign {
    int lfo = 1;
    double amount = 0;
    std::optional<int> track;

    ModAssign() = default;
    ModAssign(int l, double a, std::optional<int> t = std::nullopt) : lfo(l), amount(a), track(t) {}
    auto tie() const { return std::tie(lfo, amount, track); }
    JAMBO_EQ(ModAssign)
};

}  // namespace jambo
