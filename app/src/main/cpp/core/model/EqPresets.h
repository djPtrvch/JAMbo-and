// Пресеты EQ (порт Core/Model/EqPresets.swift, ветка swift JAMbo): кнопка PRESETS строки EQ; один список, общий для
// дорожек, мастера и проектов, хранится отдельно от проекта (на Android — файл EqPresets.json, JSON как у iPad).
// Пресет — полосы канала со всеми полями, без BYPASS.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "eq/EqModel.h"

namespace jambo {

struct EqPreset {
    std::string name;
    std::vector<std::optional<EqBand>> bands;
    int count() const;
};

struct EqPresetBook {
    std::vector<EqPreset> list;
    std::string nextName() const;
    std::optional<int> index(const std::string& name, int except = -1) const;
    void save(const std::string& name, const Eq& eq, std::optional<int> over = std::nullopt);
    void rename(int k, const std::string& name);
    void remove(int k);
    void move(int from, int to);
    static EqPresetBook decode(const std::string& data);
    std::string encode() const;
};

}  // namespace jambo
