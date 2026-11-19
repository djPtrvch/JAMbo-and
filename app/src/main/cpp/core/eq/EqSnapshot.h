// EQ для снимка звука (порт части EqSpec, Core/EQ/EqModel.swift, ветка swift JAMbo): полосы → плоский массив Double
// колбэка, порядок дорожек в микшере для динамических полос.
#pragma once
#include <vector>
#include "EqModel.h"

namespace jambo {
struct Project;
namespace EqSpec {
/// Динамическая полоса (bell, shelf) с источником.
bool dynOn(const EqBand& b);
/// Число биквадов в каскаде полосы.
int stages(const EqBand& b);
/// Q для фильтра: у low cut / high cut — в dB (резонанс).
double filterQ(const EqBand& b);
/// EQ дорожки t слушает другую дорожку (включённая динамическая полоса с источником T1…T16, не своим).
bool listensToTrack(const Project& project, int t, int count);
/// Порядок дорожек в микшере: каждая — после дорожек, которые слушают её динамические полосы.
std::vector<int> mixOrder(const Project& project, int count);
/// Канал EQ → stride значений (eq nullptr — нули).
void fill(double* p, const Eq* eq);
}  // namespace EqSpec
}  // namespace jambo
