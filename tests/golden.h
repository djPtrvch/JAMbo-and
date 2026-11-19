// Сверка с эталонами (порт ios/CoreTests/Tests/CoreTests/Golden/Golden.swift JAMbo).
#pragma once
#include <string>
#include <vector>

namespace golden {

struct Result {
    std::string file;
    double sampleErrorDb = 0;   // ошибка по отсчётам, dB к RMS эталона
    double maxBandDiffDb = 0;   // наибольшее отличие третьоктавной полосы, dB
    double rmsDb = 0;           // RMS эталона, dBFS
    bool finite = true;
    /// Критерий: по отсчётам ≤ −60 dB ИЛИ полосы 20 Hz–20 kHz в пределах ±0,5 dB.
    bool passes() const { return sampleErrorDb <= -60 || maxBandDiffDb <= 0.5; }
    /// Итог: критерий + страховка (по отсчётам не хуже −40 dB), без NaN, не тишина (RMS выше −60 dBFS).
    bool ok() const { return passes() && finite && sampleErrorDb <= -40 && rmsDb > -60; }
};

std::string dir();
/// Эталон: Int16 → −1…1 (каналы перемежаются).
std::vector<float> load(const std::string& file);
Result compare(const std::string& file, const std::vector<float>& actual, const std::vector<float>& golden);
/// Как в эталоне: ±1, Int16.
void quantize(std::vector<float>& x);

}  // namespace golden
