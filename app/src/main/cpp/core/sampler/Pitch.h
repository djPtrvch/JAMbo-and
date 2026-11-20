// Определение высоты сэмпла при загрузке (порт Core/Sampler/Pitch.swift, ветка swift JAMbo): YIN по нескольким
// окнам, только для индикации. Шумные и ударные звуки без ясной высоты — nullopt.
#pragma once
#include <optional>
#include <vector>
#include "SampleData.h"

namespace jambo {
namespace PitchDetect {
/// MIDI-нота сэмпла или nullopt.
std::optional<int> root(const SampleData& s);
std::optional<double> yin(const std::vector<float>& x, int start, double rate, int win);
}  // namespace PitchDetect
}  // namespace jambo
