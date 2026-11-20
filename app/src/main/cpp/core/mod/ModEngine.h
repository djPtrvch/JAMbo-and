// Модуляция дорожек (порт Core/Mod/ModEngine.swift, ветка swift JAMbo): LFO1–3 (идут всегда; Random — новое значение
// на каждом цикле) и огибающая ENV (запускается каждой нотой, гейт — длина ноты; живая — пока держат клавишу).
// Назначение: значение ручки в 0…1 + степень · выход источника (LFO −1…+1, ENV 0…1), с ограничением 0…1.
// Колбэк: begin, publish, env, source, trig, liveOn/Off, value — без выделений (массивы созданы заранее).
#pragma once
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include "fx/FxRandom.h"
#include "sampler/SoundSnapshot.h"
#include "util/AtomicFloat.h"

namespace jambo {

class ModEngine {
public:
    static constexpr int tracks = SoundData::maxTracks;
    static constexpr int envAttack = 12, envDecay = 13, envSustain = 14, envRelease = 15;
    static constexpr int64_t gateLive = std::numeric_limits<int64_t>::max();

    explicit ModEngine(double sampleRate, uint32_t seed = randomSeed());
    static uint32_t randomSeed();

    /// Форма LFO: wave 0 Sine, 1 Triangle, 2 Saw, 3 Square, иначе Random.
    static double shape(int wave, double ph, double r);

    /// Начало блока: параметры MOD с p-lock играющего шага, выход LFO и шаг фаз на n отсчётов.
    void begin(const SoundData& d, int n, const uint16_t* lockMask, const float* lockVal, double bpm = 120);
    /// Выходы ENV для интерфейса — в конце блока.
    void publish(int64_t frame);
    /// Уровень ADSR (0…1) в кадре frame.
    double env(int t, int64_t frame) const;
    /// Выход источника (1–3 — LFO, 4 — ENV) дорожки t в кадре frame.
    double source(int t, int lfoN, int64_t frame) const;
    /// Нота дорожки t в кадре frame: ENV с начала; gate — кадров удержания (gateLive — живая).
    void trig(int t, int64_t frame, int64_t gate);
    void liveOn(int t, int64_t frame);
    void liveOff(int t, int64_t frame);
    /// Значение цели j дорожки t с модуляцией (base — ручка или p-lock).
    double value(int t, int j, double base, int64_t frame) const;
    bool modulated(int t) const;
    /// Перестановка дорожки во время игры: фазы LFO и ход ENV — вместе с дорожкой.
    template <class M> void moveTrack(const M& mv) {
        for (auto* v : {&phase, &rnd, &lfo, &p, &divBeats}) mv.applyPerTrack(*v, tracks);
        mv.applyPerTrack(envStart, tracks);
        mv.applyPerTrack(envGateEnd, tracks);
        mv.applyPerTrack(live, tracks);
    }

    /// Выходы источников для интерфейса: дорожка·4 + (LFO1–3, ENV).
    std::unique_ptr<AtomicFloat[]> outs;
    /// Фазы LFO (дорожка·3 + n); ENV — мс от запуска (−1 — нет) и гейт в мс (живая — 1e9).
    std::unique_ptr<AtomicFloat[]> phaseOuts;
    std::unique_ptr<AtomicFloat[]> envOuts;

private:
    double sr;
    FxRandom rng;
    std::vector<double> phase, rnd, lfo, p, divBeats;
    std::vector<int64_t> envStart, envGateEnd;
    std::vector<int> live;
    const SoundData* data = nullptr;
};

/// Миниатюра ENV (EnvDot.swift): форма по ручкам с условным гейтом, точка — всегда на кривой. Всё в миллисекундах.
namespace EnvDot {
double curve(double a, double d, double s, double r, double x, double gate);
double ads(double a, double d, double s, double y);
/// Точка на кривой: t — время от начала ноты, g — гейт ноты, gate — условный гейт миниатюры.
double x(double a, double d, double s, double r, double t, double g, double gate);
}  // namespace EnvDot

}  // namespace jambo
