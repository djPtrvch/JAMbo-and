// Пресет синта и звук папки дорожки (порт структур Core/Model/SynthPreset.swift, ветка swift JAMbo); чтение с
// переводом старых fmt — в ProjectCoding.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "ModAssign.h"
#include "Params.h"

namespace jambo {

/// Пресет синта: имя, движок, ручки; fmt — версия формата (5 — нынешняя).
struct SynthPreset {
    std::string name;
    std::string sound;
    ParamMap params;
    std::optional<int> fmt = 5;
    /// Таблица WTABLE; nullopt — не WTABLE.
    std::optional<std::string> table;
    /// Сэмпл GRANULAR; nullopt — не GRANULAR.
    std::optional<std::string> grain;
    /// Модуляция: ручки LFO1–3 и ENV и назначения; nullopt — прежний пресет без модуляции.
    std::optional<ParamMap> mods;
    std::optional<std::map<std::string, ModAssign>> assigns;

    /// Ручки звука, которые хранит пресет.
    static const std::vector<std::string>& paramIds();
    /// Значение ручки id; nullopt — нет.
    std::optional<double> value(const std::string& id) const;

    auto tie() const { return std::tie(name, sound, params, fmt, table, grain, mods, assigns); }
    JAMBO_EQ(SynthPreset)
};

/// Звук папки дорожки: сэмпл (ключ) или пресет синта.
struct FolderItem {
    std::optional<std::string> sample;
    std::optional<SynthPreset> preset;

    FolderItem() = default;
    static FolderItem ofSample(std::string s) { FolderItem f; f.sample = std::move(s); return f; }
    static FolderItem ofPreset(SynthPreset p) { FolderItem f; f.preset = std::move(p); return f; }
    /// Ключ звука: сэмпл или движок пресета.
    std::string key() const { return sample ? *sample : preset ? preset->sound : std::string(); }

    auto tie() const { return std::tie(sample, preset); }
    JAMBO_EQ(FolderItem)
};

}  // namespace jambo
