// Звук: Oboe (AAudio), 48 кГц, low latency. Весь звук — Core (jambo::AudioCore::render в колбэке); поверх — тестовый
// тон и щелчок TAP спайка. События и снимки — от App (jambo::AppAudio). Колбэк без выделений памяти и блокировок.
#pragma once
#include <cstring>
#include <oboe/Oboe.h>
#include <oboe/LatencyTuner.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdint>
#include <vector>
#include "app/App.h"
#include "engine/AudioCore.h"

class LinkSession;

class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback,
                    public jambo::AppAudio {
public:
    explicit AudioEngine(LinkSession* link) : link_(link) {}
    bool start();
    void stop();

    void setTone(bool on) { tone_.store(on); }
    void tap(int64_t touchNanos);           // время касания, CLOCK_MONOTONIC
    // jambo::AppAudio (основной поток; события и снимки — под produce_).
    void push(const jambo::CoreEvent& e) override;
    double modOut(int i) const override;
    jambo::PlayClock::State clock() const override;
    double heardNow() const override;
    /// Задержка выхода (для heardNow) — из основного потока, раз в секунду.
    void updateLatency();
    jambo::Levels* levels() override { return core_ ? &core_->levels : nullptr; }
    jambo::Spectra* spectra() override { return core_ ? &core_->spectra : nullptr; }
    double eqDynOffset(int ch, int k) const override { return core_ ? core_->eqDyn.offset(ch, k) : 0; }
    const jambo::Levels* levels() const override { return core_ ? &core_->levels : nullptr; }
    double modPhase(int i) const override;
    double envOut(int i) const override;
    void publish(const jambo::Project& p, const jambo::SoundSnapshot::Options& o,
                 const std::map<std::string, jambo::SampleRef>& samples) override;
    bool popOut(jambo::SeqOut& e) override;
    bool popVoice(jambo::VoiceOut& e) override;
    jambo::MasterChain* master() override { return core_ ? &core_->master : nullptr; }
    jambo::EqListen* eqListen() override { return core_ ? &core_->eqListen : nullptr; }
    jambo::StemRing* stemRing() override { return core_ ? &core_->sequencer.stemRing : nullptr; }
    long framesRendered() const override { return core_ ? core_->framesRendered() : 0; }

    // Заполняет out (kInfoSize значений), см. порядок в audio_engine.cpp.
    static constexpr int kInfoSize = 20;
    void info(double* out);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t frames) override;
    void onErrorAfterClose(oboe::AudioStream* s, oboe::Result r) override;

private:
    bool openLocked();

    LinkSession* link_;
    std::mutex lock_;
    std::shared_ptr<oboe::AudioStream> stream_;
    /// Буфер растёт на бёрст при каждом XRun (щелчки на перегрузке колбэка).
    std::unique_ptr<oboe::LatencyTuner> tuner_;
    bool wanted_ = false;

    // UI/MIDI -> аудио
    std::atomic<bool> tone_{false};
    std::atomic<uint32_t> tapSeq_{0};
    std::atomic<int64_t> tapTouchNs_{0};
    /// Ядро: события и снимки пишет один поток за раз (produce_), колбэк читает без блокировок.
    std::unique_ptr<jambo::AudioCore> core_;
    std::mutex produce_;
    std::vector<float> coreL_, coreR_;
    void ensureCore();
    std::atomic<int64_t> outLatencyUs_{0};

    // аудио -> UI
    std::atomic<int64_t> tapFrame_{-1};
    std::atomic<int64_t> tapTouchSeen_{0};
    std::atomic<float> loadAvg_{0}, loadMax_{0};
    std::atomic<int32_t> lastFrames_{0};

    // состояние колбэка
    double sr_ = 48000.0;
    int64_t frameCount_ = 0;
    float toneGain_ = 0, tonePhase_ = 0;
    uint32_t tapSeen_ = 0;
    float clickEnv_ = 0, clickPhase_ = 0, clickFreq_ = 1500;
    float loadMaxHold_ = 0;
    int loadMaxTicks_ = 0;
};
