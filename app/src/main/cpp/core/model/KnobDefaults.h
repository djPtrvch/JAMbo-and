// Свои значения ручек по умолчанию (порт Core/Model/KnobDefaults.swift, ветка swift JAMbo): SHIFT + удержание ручки —
// запомнить, двойной тап — сбросить; SAVE DEFAULT в PRESETS окон мастера и EQ. Общие для проектов (хранит экран).
// Действуют при сбросе, в новом проекте и на новой дорожке. JSON — с ProjectCoding.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Params.h"
#include "../eq/EqModel.h"

namespace jambo {

struct Track;
struct Project;

struct KnobDefaults {
    /// Ключ «область/id»: sampler или synthN, mod, fx, mst.
    ParamMap values;
    /// Свой EQ по умолчанию: полосы EQ дорожки и мастера; nullopt — нет своего.
    std::optional<std::vector<std::optional<EqBand>>> eqTrack;
    std::optional<std::vector<std::optional<EqBand>>> eqMaster;
    /// 2 — Filter на всём ходу; nullopt — прежняя двухполярная ручка (переводится при чтении).
    std::optional<int> filterV = 2;

    bool isEmpty() const { return values.empty() && !eqTrack && !eqMaster; }
    void setEq(const std::vector<std::optional<EqBand>>& bands, bool master) {
        if (master) eqMaster = bands; else eqTrack = bands;
    }

    static std::string soundScope(const std::string& sound);
    static std::string key(const std::string& scope, const std::string& id) { return scope + "/" + id; }
    /// Что запоминается вместе со значением ручки; пусто — ничего.
    static std::string companion(const std::string& id);

    std::optional<double> value(const std::string& scope, const std::string& id) const;
    /// Звук и MOD дорожки — по своим значениям.
    void apply(Track& t) const;
    /// Ручки звука дорожки — по своим значениям его области.
    void applySound(Track& t) const;
    /// Новый проект: FX, мастер и дорожки.
    void apply(Project& p) const;
    /// Прежняя ручка Filter (до 9.11) — тип по её стороне и частота.
    void migrateFilter();

    auto tie() const { return std::tie(values, eqTrack, eqMaster, filterV); }
    JAMBO_EQ(KnobDefaults)
};

}  // namespace jambo
