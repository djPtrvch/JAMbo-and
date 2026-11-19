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

}  // namespace jambo
