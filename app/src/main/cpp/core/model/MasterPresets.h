// Пресеты модулей мастера (порт Core/Model/MasterPresets.swift, ветка swift JAMbo): кнопка PRESETS в окнах SAT, COMP,
// CLIP, LIM; общие для проектов, хранятся отдельно от проекта (на Android — файл MasterPresets.json, JSON как у iPad);
// у каждого модуля свой список. Пресет — значения параметров модуля, без BYPASS.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "Params.h"

namespace jambo {

struct MasterPreset {
    std::string name;
    ParamMap values;
};

struct MasterPresetBook {
    static const std::array<const char*, 4> prefixes;
    static const std::array<const char*, 4> titles;
    static constexpr int nameMax = 40;
    std::array<std::vector<MasterPreset>, 4> lists;

    static std::vector<std::string> ids(int m);
    const std::vector<MasterPreset>& list(int m) const;
    std::string nextName(int m) const;
    static std::optional<std::string> clean(const std::string& name);
    std::optional<int> index(int m, const std::string& name, int except = -1) const;
    void save(int m, const std::string& name, const ParamMap& master, std::optional<int> over = std::nullopt);
    void rename(int m, int k, const std::string& name);
    void remove(int m, int k);
    void move(int m, int from, int to);
    void apply(int m, int k, ParamMap& master) const;
    static MasterPresetBook decode(const std::string& data);
    std::string encode() const;
};

}  // namespace jambo
