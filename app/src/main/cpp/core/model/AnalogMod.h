// ANALOG: модуляция B осциллятором A и октава B — скрытые значения звука (порт Core/Model/AnalogMod.swift, ветка swift
// JAMbo): ключи, режимы, подписи ручек 1.2 и 1.3 по режиму.
#pragma once
#include <string>
#include <vector>
#include "Params.h"

namespace jambo {
namespace AnalogMod {
/// Режим модуляции: 0 — нет; 1 SYNC, 2 FM, 3 RING, 4 AM — включена; −1…−4 — выключена, помнит выбранный режим.
constexpr const char* modId = "analog.mod";
/// Октава B: 1 — на октаву ниже.
constexpr const char* octId = "analog.oct";
/// Имена режимов SYNC, FM, RING, AM (подпись ручки 1.3 при включённой модуляции).
extern const std::vector<std::string> names;
constexpr int am = 4;
/// SYNC: AMOUNT 0 → 100 % — высота B 0 → +24 полутона.
constexpr double syncSt = 24.0;

/// Действующий режим по значению: 0 — нет, 1…4 — SYNC, FM, RING, AM.
inline int mode(double v) {
    if (v < 0.5) return 0;
    const int m = int(v + 0.5);
    return m < 4 ? m : 4;
}
/// SHIFT + тап по WAVE A: вкл. ↔ выкл. (выбранный режим запоминается).
double toggled(double v);
/// SHIFT + тап по 1.3: следующий режим (back — предыдущий); выключена — как есть.
double cycled(double v, bool back = false);
/// Ручка 1.1–1.4 синта с учётом режима ANALOG (иначе — p как есть).
const ParamDef& knob(const ParamDef& p, int engine, const ParamMap& params);
}  // namespace AnalogMod
}  // namespace jambo
