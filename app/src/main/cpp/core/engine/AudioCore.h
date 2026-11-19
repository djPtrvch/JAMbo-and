// Весь звук приложения (порт Core/Engine/AudioCore.swift, ветка swift JAMbo): один render на блок, из колбэка Oboe.
// В render — только то, что разрешено правилами колбэка: без выделений, блокировок, логов и JNI.
// Снимки паттернов и звука: новые — в колбэк, прежние — обратно; освобождаются в основном потоке не раньше чем через
// keepAlive с (голоса, которые гаснут, ещё читают их сэмплы).
// Отличие от Swift: снимки передаются сырыми указателями через очереди (в Swift — Unmanaged); время для keepAlive —
// аргумент releaseRetired (секунды основного потока).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "CoreEvent.h"
#include "Levels.h"
#include "RefPlayer.h"
#include "TestTone.h"
#include "engine/RenderPool.h"
#include "eq/EqDyn.h"
#include "eq/EqListen.h"
#include "eq/Spectrum.h"
#include "eq/TrackEqs.h"
#include "fx/FxRack.h"
#include "master/MasterChain.h"
#include "mod/ModEngine.h"
#include "monitor/MonitorChain.h"
#include "sampler/Sampler.h"
#include "sampler/SoundSnapshot.h"
#include "sequencer/Arpeggiator.h"
#include "sequencer/SeqSnapshot.h"
#include "sequencer/Sequencer.h"
#include "synth/SynthVoices.h"
#include "util/CPUMeter.h"
#include "util/RenderProfile.h"
#include "util/SPSCQueue.h"

namespace jambo {

class AudioCore {
public:
    /// Частота ядра: всегда 48 кГц.
    static constexpr double sampleRate = 48000;
    static constexpr int maxFrames = 4096;
    static constexpr double keepAlive = 0.5;

    explicit AudioCore(bool master = true);
    ~AudioCore();
    AudioCore(const AudioCore&) = delete;
    AudioCore& operator=(const AudioCore&) = delete;

    /// UI → аудио.
    SPSCQueue<CoreEvent> events{1024, CoreEvent()};
    CPUMeter cpu{sampleRate};
    RenderProfile profile{sampleRate};
    long framesRendered() const { return frameCounter.load(); }

    Sequencer sequencer{sampleRate, 120};
    Sampler sampler{sampleRate};
    FxRack fx{sampleRate};
    ModEngine mod{sampleRate};
    Arpeggiator arp;
    TrackEqs trackEqs{sampleRate};
    EqDynLevels eqDyn;
    EqListen eqListen{sampleRate};
    Spectra spectra;
    Levels levels;
    MasterChain master{sampleRate};
    RefPlayer ref;
    MonitorChain monitor{sampleRate};
    RenderPool pool;

    /// Новые снимки (основной поток, после каждой правки): звук — всегда, паттерны — если изменились. now — секунды
    /// основного потока (для keepAlive). В Options поле slots ставит само ядро.
    void publish(const Project& project, const std::map<std::string, SampleRef>& samples = {},
                 SoundSnapshot::Options o = {}, double now = 0);
    /// Голоса синта (основной поток, один раз).
    void installSynth();
    /// Освободить снимки, которые колбэк больше не использует (основной поток; now — секунды).
    void releaseRetired(double now);
    /// Колбэк: блок frames (≤ maxFrames) кадров; host — время хоста (с), когда блок начнёт звучать.
    void render(int frames, float* left, float* right, double host = 0);
    int64_t sampleTime() const { return sampleTime_; }

private:
    bool useMaster;
    std::vector<float> masterL, masterR;
    SPSCQueue<SeqSnapshot*> snapshots{64, nullptr};
    SPSCQueue<SeqSnapshot*> retired{256, nullptr};
    SeqSnapshot* current = nullptr;
    SPSCQueue<SoundSnapshot*> sounds{256, nullptr};
    SPSCQueue<SoundSnapshot*> retiredSounds{512, nullptr};
    SoundSnapshot* currentSound = nullptr;
    struct Waiting {
        double time;
        SeqSnapshot* seq;
        SoundSnapshot* sound;
    };
    std::vector<Waiting> waiting;
    std::unique_ptr<Project> lastSeq;
    std::map<int, int> altKeep;
    SPSCQueue<SynthVoices*> synthQueue{2, nullptr};
    bool synthInstalled = false;
    std::unique_ptr<SynthVoices> synthOwned;
    TestTone tone{sampleRate};
    int64_t sampleTime_ = 0;
    AtomicCounter frameCounter;

    void renderArp(int frames);
    void renderMix(int frames, float* left, float* right);
    void renderPhones(int frames, float* left, float* right);
    void renderRef(const SoundData& d, int n);
    void captureStems(StemRing& ring, int n, const float* ml, const float* mr);
    void meterMaster(int n);
    void takeSnapshots();
    void apply(const CoreEvent& e);
};

}  // namespace jambo
