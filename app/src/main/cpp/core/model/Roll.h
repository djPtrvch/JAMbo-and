// Пианоролл (порт Core/Model/Roll.swift и StepCarry из RollEdits.swift, ветка swift JAMbo).
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Clip.h"

namespace jambo {

/// Нота на пианоролле: позиция в проходе, шаг клипа (p·16 + s), номер ноты в шаге, высота, длина, velocity.
struct RollNote {
    int pos, index, k, pitch, len;
    std::optional<double> vel;
    auto tie() const { return std::tie(pos, index, k, pitch, len, vel); }
    JAMBO_EQ(RollNote)
};

/// P-lock и свой звук шага в пути с нотой пианоролла; at — шаг, где они сейчас (nullopt — «в руке»).
struct StepCarry {
    ParamMap locks;
    std::optional<std::string> sound;
    std::optional<std::vector<std::string>> soundLocks;
    std::optional<int> at;
    auto tie() const { return std::tie(locks, sound, soundLocks, at); }
    JAMBO_EQ(StepCarry)
};

namespace Roll {
/// GRID: шагов в клетке сетки (1/16, 1/8, 1/4).
extern const std::vector<int> grids;
extern const std::vector<std::string> gridNames;
constexpr int gridDef = 1;

/// Ноты гаммы 0…127 снизу вверх.
std::vector<int> rows(int scale, int root);
/// Диез (чёрная клавиша).
bool isSharp(int n);
bool isRoot(int n, int root);
/// Шаг клипа на каждой позиции прохода.
std::vector<int> positions(const Clip& clip);
/// Ноты прохода по порядку позиций.
std::vector<RollNote> notes(const Clip& clip);
/// Подпись доли: такт.доля, с 1.
std::string beatLabel(int pos);
/// Позиция n в начало клетки сетки.
int snap(int n, int grid);
/// Длина по сетке: кратна GRID, не меньше 1, не дальше конца прохода (cols − pos).
int snapLen(double steps, int grid, int pos, int cols);
}  // namespace Roll

}  // namespace jambo
