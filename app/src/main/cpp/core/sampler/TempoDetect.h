// Темп файла и атаки сэмпла (порт Core/Sampler/TempoDetect.swift и Onsets.swift, ветка swift JAMbo): для TSTR
// и варпинга; считаются один раз на сэмпл, в основном потоке (SampleData::loopBpm, onsets).
#pragma once
#include <optional>
#include <vector>

namespace jambo {

class SampleData;

namespace TempoDetect {
constexpr int hop = 256;
constexpr double lo = 75, hi = 180;
/// Темп лупа для TSTR: по длине (луп в 1, 2, 4… такта, 85…170 BPM), у файла от 6 с — и по атакам; короче такта при
/// 170 BPM — 0.
double loopBpm(const SampleData& s);
/// Темп, BPM; nullopt — не нашёлся.
std::optional<double> bpm(const SampleData& s);
double score(const std::vector<double>& r, double bpm, double fr, double span);
double interp(const std::vector<double>& r, double x);
std::vector<double> envelope(const SampleData& s, int from, int count);
std::vector<double> autocorr(const std::vector<double>& x, int maxLag);
}  // namespace TempoDetect

/// Атаки сэмпла (позиции в отсчётах, по возрастанию).
struct OnsetList {
    std::vector<double> at;
    int count() const { return int(at.size()); }
};

namespace Onsets {
constexpr int hop = 128;
std::vector<double> detect(const SampleData& s);
/// Начало подъёма около кадра атаки.
double start(const SampleData& s, int at);
}  // namespace Onsets

}  // namespace jambo
