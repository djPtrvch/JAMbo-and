// Тип фильтра голоса: ручка Filter — частота на всём ходу, SHIFT + тап — тип. У всех движков и сэмплера —
// LP → HP → BP (12 dB); у SUB — LP → LADDER → BP. Порт Core/Model/FilterType.swift (ветка swift JAMbo); перевод
// дорожек, клипов и кривых (FilterMigration.clip, lanes, used, Project.migrateFilter) — с моделью проекта.
#pragma once
#include <string>
#include <vector>
#include "Params.h"

namespace jambo {

namespace FilterType {
constexpr int lp = 0;
constexpr int hp = 1;
constexpr int bp = 2;
/// Лестничный фильтр 24 dB: только у SUB.
constexpr int ladder = 3;
extern const std::vector<std::string> names;

/// Тип по значению звука (0…3; другое, нет или не число — LP).
int of(const double* v);
inline int of(double v) { return of(&v); }
/// Тип, который звучит у движка: LADDER — только у SUB (у других — LP).
int effective(int type, const std::string& sound);
/// Следующий тип по SHIFT + тапу; back — предыдущий.
int next(int type, const std::string& sound, bool back = false);
/// Значение ручки при смене типа: открытый фильтр — открытый и у нового типа, иначе частота прежняя.
double valueAfterSwitch(double v, int from, int to);
}  // namespace FilterType

/// Перевод прежней двухполярной ручки Filter (до 9.11) в тип и частоту на всём ходу.
namespace FilterMigration {
/// Тип прежнего значения: −1 — выключен (центр).
int oldType(double v);
/// Новое значение прежней ручки v у звука с типом type.
double value(double v, int type);
/// Тип звука: по ручке звука (base, nullptr — нет); выключен — по большинству прежних значений шагов и кривых.
int soundType(const double* base, const std::vector<double>& others);
/// Ручки звука: Filter — по типу, тип — в Params::filterTypeId.
void params(ParamMap& p, int type);
/// Пресет: тип — по его ручке Filter (нет ручки — ничего не меняется).
void preset(ParamMap& p);
}  // namespace FilterMigration

}  // namespace jambo
