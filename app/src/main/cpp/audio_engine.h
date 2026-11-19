// Звук: Oboe (AAudio), 48 кГц, low latency. Весь звук — Core (jambo::AudioCore::render в колбэке); поверх — тестовый
// тон и щелчок TAP спайка. Ноты MIDI и PLAY — события ядра. Колбэк без выделений памяти и блокировок.
#pragma once
#include <cstring>
#include <oboe/Oboe.h>
#include <oboe/LatencyTuner.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdint>
#include <vector>
#include "engine/AudioCore.h"

class LinkSession;

class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback {
public:
    explicit AudioEngine(LinkSession* link) : link_(link) {}
    bool start();
    void stop();

    void setTone(bool on) { tone_.store(on); }
    void tap(int64_t touchNanos);           // время касания, CLOCK_MONOTONIC
    void noteOn(int note, int velocity);
    void noteOff(int note);
    /// PLAY проверочного проекта (дорожка 1 — DRUM, дорожка 2 — ANALOG).
    void play(bool on);

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
    void push(const jambo::CoreEvent& e);
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
