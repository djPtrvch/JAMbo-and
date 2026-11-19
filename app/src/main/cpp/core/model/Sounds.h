// Звук дорожки: папка звуков и маркеры нарезки (порт структур Core/Model/Sounds.swift, ветка swift JAMbo).
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "SynthPreset.h"

namespace jambo {

/// Папка дорожки: сэмплы и пресеты синта, номер звучащего (−1 — нет).
struct Folder {
    std::vector<FolderItem> items;
    int index = -1;

    /// Ключи сэмплов папки (без пресетов).
    std::vector<std::string> sampleKeys() const {
        std::vector<std::string> v;
        for (const auto& it : items) if (it.sample) v.push_back(*it.sample);
        return v;
    }
    auto tie() const { return std::tie(items, index); }
    JAMBO_EQ(Folder)
};

/// Маркер нарезки: доля длины сэмпла 0…1, нота (nullopt — по порядку), роль для случайной нарезки, free — без ноты.
struct SliceMark {
    double at = 0;
    std::optional<int> note;
    std::optional<int> role;
    std::optional<bool> free;

    SliceMark() = default;
    SliceMark(double a, std::optional<int> n = std::nullopt, std::optional<int> r = std::nullopt, bool f = false)
        : at(a), note(n), role(r), free(f ? std::optional<bool>(true) : std::nullopt) {}
    bool isFree() const { return free == true; }
    auto tie() const { return std::tie(at, note, role, free); }
    JAMBO_EQ(SliceMark)
};

/// SETTINGS → SAMPLER → SLICE NOTES.
enum class SliceMap { chromatic, scale };

/// Ноты маркеров нарезки (Slices, Sounds.swift): Slices.cpp
namespace Slices {
/// «Нота» метки без ноты.
constexpr int noNote = -1;
/// ПРОБА Swift-версии: ноты по порядку — всегда от 3-й октавы (fixedOctave), не от октавы клавиатуры.
constexpr int fixedOctave = 3;
/// Нота по порядку маркера idx: CHROMATIC — по полутону, SCALE — ступени гаммы.
int note(int idx, SliceMap map, int scale, int root, int octave = 3);
/// Ноты маркеров: назначенная — как есть; остальные — по порядку, занятая пропускается; без ноты — noNote.
std::vector<int> notes(const std::vector<SliceMark>& m, SliceMap map, int scale, int root, int octave = 3);
}  // namespace Slices

/// SETTINGS → SIDECHAIN: какие посылы приглушаются вместе с дорожкой (Settings.swift).
struct SidechainSends {
    bool rev = false, dly = false, drv = true;
};

}  // namespace jambo
