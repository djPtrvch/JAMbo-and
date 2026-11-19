// Эффекты дорожек и Sidechain (порт Core/FX/FxRack.swift, ветка swift JAMbo): у каждой дорожки свои Reverb, Delay,
// Drive; посылы (после громкости и mute/solo дорожки) → эффекты дорожки → выход (Drive — с усилением 0,5). Значения — FX
// клипа, который играет на дорожке, с p-lock шагов своей дорожки и LFO/ENV. Огибающая Sidechain (Duck) одна на проект.
// Эффекты дорожки считаются, только пока в них есть звук (покой), — заданиями RenderPool. Буферы выделены при создании.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "Delay.h"
#include "Drive.h"
#include "Duck.h"
#include "Reverb.h"
#include "engine/RenderPool.h"
#include "mod/ModEngine.h"
#include "sampler/SoundSnapshot.h"

namespace jambo {

class FxRack : public RenderJobs {
public:
    static constexpr float driveOut = 0.5f;
    static double maxDelay() { return 4 * 60 / Params::bpmMin; }
    static constexpr int tracks = SoundData::maxTracks;
    static constexpr float idleLevel = 1e-6f;
    static constexpr double revIdleSec = 1;
    static constexpr double odIdleSec = 0.1;
    /// Номера FX в Params::fxIds.
    static constexpr int revSize = 0, dlyTime = 1, odDrive = 2, scAttack = 3;
    static constexpr int revDecay = 4, dlyFeedback = 5, odType = 6, scRelease = 7;
    static constexpr int revTone = 8, dlyFilter = 9, odTone = 10, scSource = 11;
    static constexpr int revHpf = 12, dlySpread = 13, odLowcut = 14, scCurve = 15, revPredelay = 16, dlySync = 17;

    explicit FxRack(double sampleRate, int maxFrames = 4096);
    /// seed эффекта (случайный, как FxRandom.seed()).
    static uint32_t seed();

    std::vector<std::unique_ptr<FxReverb>> reverb;
    std::vector<std::unique_ptr<FxDelay>> delay;
    std::vector<std::unique_ptr<FxDrive>> drive;
    FxDuck duck;
    /// Входы эффектов дорожки (посылы после громкости): (дорожка·3 + посыл)·maxFrames; fed — вход записан в этом блоке.
    std::vector<float> inL, inR;
    std::vector<uint8_t> fed;
    /// Огибающая e на блок.
    std::vector<float> e;
    /// Общие значения FX на блок и значения FX дорожек (дорожка·fxCount).
    std::vector<double> live, tlive;
    int source = 0;
    bool anyDucked = false;
    /// Многоядерный режим (ставит AudioCore).
    RenderPool* pool = nullptr;

    bool revIdle(int t) const { return idle[size_t(t * 3)] != 0; }
    bool dlyIdle(int t) const { return idle[size_t(t * 3 + 1)] != 0; }
    bool odIdle(int t) const { return idle[size_t(t * 3 + 2)] != 0; }

    /// Начало блока: общие FX (Duck и источник) и FX дорожек, p-lock, LFO/ENV.
    void begin(const SoundData& d, const uint32_t* lockMask, const float* lockVal, const ModEngine* mod = nullptr,
               int64_t frame = 0);
    /// Нота дорожки t в кадре frame: триг Sidechain, если это источник и хоть одна другая дорожка приглушается.
    void noteTrig(int t, int64_t frame);
    /// Огибающая e на блок; входы дорожек — не записаны.
    void prepare(int64_t blockStart, int n);
    /// Эффекты дорожек: входы → выходы → сумма к left/right.
    void process(int n, double bpm, float* left, float* right);
    void runJob(int j) override { processTrack(jobTracks[size_t(j)]); }
    /// Остановка: будущие триги Sidechain отменяются.
    void flush() { duck.flush(); }

private:
    double sr;
    int maxFrames;
    double dlyIdleSec;
    std::vector<float> outL, outR, tmpL, tmpR;
    std::vector<float> amount, sendFlag;
    std::vector<int> revQuiet, dlyQuiet, odQuiet;
    std::vector<uint8_t> idle;
    std::vector<double> delayBeats;
    int delayDivs = 0;
    std::vector<int> jobTracks;
    int jobN = 0;
    double jobBpm = 120;
    int trackCount = 0;

    void processTrack(int t);
};

}  // namespace jambo
