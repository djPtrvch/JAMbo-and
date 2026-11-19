// Таблицы своих движков (порт JamboTables из Core/Jambo/JamboTables.swift): синус, полуполосный фильтр,
// нормировка форм ANALOG. Считаются один раз вне колбэка.
#pragma once
#include <vector>

namespace jambo {

class JamboTables {
public:
    JamboTables();
    static const JamboTables& shared();

    /// Синус 4096 + 1 точек (значения Float, хранятся в Double).
    std::vector<double> sineD;
    /// Полуполосный фильтр: hbHK[j] — отводы центр ∓ (2j + 1), j = 0…15; центр — hbCK (значения Float в Double).
    double hbHK[16];
    double hbCK;
    /// Нормировка пика смеси соседних форм ANALOG: [сегмент 0…2][шаг 0…64] (Float).
    std::vector<float> waveGain;
    /// Нормировка чистых форм (синус, треугольник, пила, квадрат).
    float shapeGain[4];
};

}  // namespace jambo
