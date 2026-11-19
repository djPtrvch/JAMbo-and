// EQ дорожек в колбэке (порт Core/EQ/TrackEqs.swift, ветка swift JAMbo): EQ стоит до эффектов — на дорожку один EqUnit
// с 4 линиями: шина дорожки и шины посылов Reverb, Delay, Drive. Дорожка без голосов, у которой хвост фильтров ещё
// звучит, считается дальше. Связь с микшером сэмплера — Sampler::wakeEqs, idleEq, trackEq (SamplerHooks.cpp).
#pragma once
#include <memory>
#include <vector>
#include "EqUnit.h"
#include "sampler/SoundSnapshot.h"

namespace jambo {

class TrackEqs {
public:
    static constexpr int lanes = 4;
    explicit TrackEqs(double sampleRate) {
        for (int t = 0; t < SoundData::maxTracks; ++t) units.push_back(std::make_unique<EqUnit>(sampleRate, lanes));
    }
    EqUnit& unit(int t) { return *units[size_t(t)]; }
    /// Хвост EQ дорожки t ещё звучит (в какой-нибудь линии).
    bool ringing(int t) const {
        for (int k = 0; k < lanes; ++k)
            if (units[size_t(t)]->ringing(k)) return true;
        return false;
    }

private:
    std::vector<std::unique_ptr<EqUnit>> units;
};

}  // namespace jambo
