// Длина прохода клипа в тиках транспорта (порт SeqData.passTicks, Core/Sequencer/SeqSnapshot.swift, ветка swift
// JAMbo; остальное — с секвенсором).
#pragma once
#include <algorithm>

namespace jambo {
namespace SeqData {
/// Нот на шаге самое большее.
constexpr int maxNotes = 8;
/// Проход в тиках (шестнадцатых) с учётом скорости rate (степень двойки).
inline int passTicks(int len, int rate) {
    if (rate > 0) return std::max(1, (len + 1) / 2);
    return len << (-rate);
}
}  // namespace SeqData
}  // namespace jambo
