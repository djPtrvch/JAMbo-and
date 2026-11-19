// Звук спайка: Oboe (AAudio), 48 кГц, low latency. Тестовый тон, щелчок TAP,
// ноты MIDI (синус), щелчок по долям Ableton Link. Колбэк без выделений памяти и блокировок.
#pragma once
#include <cstring>
#include <oboe/Oboe.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdint>

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
    void setLinkClick(bool on) { linkClick_.store(on); }

    // Заполняет out (kInfoSize значений), см. порядок в audio_engine.cpp.
    static constexpr int kInfoSize = 18;
    void info(double* out);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t frames) override;
    void onErrorAfterClose(oboe::AudioStream* s, oboe::Result r) override;

private:
    bool openLocked();
    void scheduleLinkClick(int32_t frames);  // доля Link внутри этого буфера

    LinkSession* link_;
    std::mutex lock_;
    std::shared_ptr<oboe::AudioStream> stream_;
    bool wanted_ = false;

    // UI/MIDI -> аудио
    std::atomic<bool> tone_{false};
    std::atomic<bool> linkClick_{false};
    std::atomic<uint32_t> tapSeq_{0};
    std::atomic<int64_t> tapTouchNs_{0};
    std::atomic<bool> gate_[128];
    std::atomic<uint32_t> onCount_[128];
    std::atomic<float> vel_[128];
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
    float noteEnv_[128] = {}, notePhase_[128] = {};
    uint32_t noteSeen_[128] = {};
    bool noteHeld_[128] = {};
    float loadMaxHold_ = 0;
    int loadMaxTicks_ = 0;
    int64_t pendingClickFrames_ = -1;   // щелчок Link внутри буфера
    float pendingClickFreq_ = 1000;
};
