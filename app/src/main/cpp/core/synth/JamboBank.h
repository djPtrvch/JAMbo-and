// Голоса своих движков JAMbo (порт Core/Synth/JamboBank.swift, ветка swift JAMbo): KICK, ANALOG, FM, WTABLE, SUB —
// JamboVoice; SNARE, HAT, CLAP, PERC — JamboDrumVoice; TEXTURE — TextureVoice (стерео, только первый слот дорожки),
// GRANULAR — GranularVoice (стерео, голос на слот, общее дорожки — GrainTrack). Создаются вне колбэка.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "GranularVoice.h"
#include "JamboDrums.h"
#include "JamboTables.h"
#include "JamboVoice.h"
#include "TextureVoice.h"

namespace jambo {

class JamboBank {
public:
    explicit JamboBank(int count, double sampleRate = 48000, int perTrack = 1);
    JamboBank(const JamboBank&) = delete;
    JamboBank& operator=(const JamboBank&) = delete;

    const int count;
    JamboTables jamboTables;

    /// Нота своего движка в голосе v; now — абсолютный кадр, tau — время дорожки с дрейфом.
    void jamboTrigger(int v, int e, double note, double p1, double p2, double p3, double p4, double decay, double now,
                      double tau);
    /// ANALOG: режим модуляции и октава B голоса v.
    void jamboAnalog(int v, int mode, bool oct) { jambo[size_t(v)]->setAnalog(mode, oct); }
    /// Ручки звучащей ноты (сглаживание — в голосе).
    void jamboSet(int v, double note, double p1, double p2, double p3, double p4, double decay);
    void jamboRender(int v, float* out, int n, double driftRatio, const WtRef& table);
    /// Голос молчал: хвоста KICK для стыка нет.
    void jamboSilent(int v);
    /// Стерео-движок (TEXTURE, GRANULAR): левый канал — в outL, правый — в outR.
    void stereoRender(int v, float* outL, float* outR, int n, int window, const GrainSrc& src);
    /// Начало блока n кадров: дрейф GRANULAR каждой дорожки и число её нот с открытым окном запуска.
    void granularBegin(int n, double sampleRate, const uint8_t* active);

private:
    std::vector<std::unique_ptr<JamboVoice>> jambo;
    std::vector<std::unique_ptr<JamboDrumVoice>> drums;
    std::vector<std::unique_ptr<TextureVoice>> texture;
    std::unique_ptr<GrainTrack[]> grainTracks;
    std::vector<std::unique_ptr<GranularVoice>> granular;
    int tracks;
    int perTrack;
    /// Вид голоса в слоте: 0 — JamboVoice, 1 — ударный, 2 — TEXTURE, 3 — GRANULAR.
    std::vector<uint8_t> kind;
};

}  // namespace jambo
