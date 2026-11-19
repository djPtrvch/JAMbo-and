// EQ дорожек, метры, спектры и замеры колбэка в сэмплере (в Swift — TrackEqs.swift, SamplerMix.swift, Levels, Spectra,
// RenderProfile). До шага 5d2 этих частей нет: поведение — как в Swift при eqs, levels, spectra, profile = nil.
#include <algorithm>
#include "Sampler.h"

namespace jambo {

void Sampler::wakeEqs(const SoundData&, int) {}

void Sampler::idleEq(int, const SoundData&, int) {}

void Sampler::trackEq(int, const SoundData&, int, bool) {}

void Sampler::parallelEqs(const SoundData&, int, bool) { std::fill(eqDone.begin(), eqDone.end(), 0); }

void Sampler::lap(int) {}

float* Sampler::meterOf(int) { return nullptr; }

bool Sampler::loudOf(int) { return false; }

void Sampler::loudProcess(const float*, const float*, int) {}

void Sampler::tapTracks(const SoundData&, int) {}

}  // namespace jambo
