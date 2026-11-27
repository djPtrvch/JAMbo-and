// Случайный ритм ударных: стили, роли и рисунки (порт Core/Model/DrumRhythm.swift, ветка swift JAMbo).
#pragma once
#include <algorithm>
#include <random>
#include "Clip.h"
#include "../synth/Jambo.h"

namespace jambo {
namespace DrumRhythm {
constexpr int styleCount = 6;  // House, Techno, Breaks, Trap, Pop, Ambient
inline int styleIndex(int s) { return std::max(0, std::min(styleCount - 1, s)); }
/// Номер стиля до 4.11 (House, Breaks, D&B, Trap, Afro, Minimal) → нынешний.
inline int migrateStyle(int old) {
    static const int map[6] = {0, 2, 2, 3, 0, 1};
    return map[std::max(0, std::min(5, old))];
}
enum Role { kick = 0, snare, hat, clap, perc };
/// Роль по номеру движка; не ударный — −1.
inline int role(int engine) {
    switch (engine) {
    case J::drum: return kick;
    case J::snare: return snare;
    case J::hat: return hat;
    case J::clap: return clap;
    case J::perc: return perc;
    default: return -1;
    }
}
inline constexpr double swing[] = {54, 50, 56, 50, 52, 50};
std::vector<int> drumBase(int role, int style, int length, std::mt19937& rng);
void writeDrums(Page& page, const std::vector<int>& base, int role, int style, int note,
                int length, bool vary, bool fill, std::mt19937& rng);
}  // namespace DrumRhythm
}  // namespace jambo
