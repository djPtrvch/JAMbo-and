// Референс-трек аранжировщика (порт Core/Engine/RefPlayer.swift, ветка swift JAMbo): файл играет в своём темпе — место
// в файле = время позиции курсора таймлайна − сдвиг начала файла; играет, только пока идёт таймлайн; скачок курсора —
// переход с кроссфейдом на блок. A/B — плавно за 20 мс; путь через модули мастера или мимо; подгонка громкости — matchGain.
#pragma once
#include <vector>
#include "Levels.h"
#include "sampler/SoundSnapshot.h"
#include "util/AtomicFloat.h"

namespace jambo {

class RefPlayer {
public:
    static constexpr int tolerance = 1200;
    static constexpr float abFrames = 960;
    static constexpr int maxFrames = 4096;

    RefPlayer();
    std::vector<float> outL, outR;
    int pos = 0;
    bool active = false;
    float ab = 0;
    AtomicCounter throughMaster, throughMonitor;
    AtomicFloat matchGain{1};

    /// Место в файле по позиции курсора x (шаги) при темпе bpm.
    static int frame(const SoundData& d, double x, double bpm);
    /// Блок n кадров: play — идёт таймлайн, x — позиция курсора в начале блока (шаги).
    void render(const SoundData& d, bool play, double x, double bpm, int n);
    /// До мастера: микс · (1 − a·m) + референс · a·m; пики и LUFS референса — в Levels::ref.
    void mixPre(bool refB, float* left, float* right, int n, Levels& levels);
    /// После мастера: референс мимо мастера (доля post).
    void mixPost(float* left, float* right, int n);

private:
    std::vector<float> rawL, rawR, post;
    const float* lastLeft = nullptr;
    float match = 1;
    float thru = 0;

    void applyMatch(int n);
    void add(const SoundData& d, int from, int n, float g, float a, float da);
};

}  // namespace jambo
