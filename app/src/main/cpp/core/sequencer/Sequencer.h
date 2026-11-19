// Секвенсор в аудиоколбэке (порт Core/Sequencer/Sequencer.swift, ветка swift JAMbo): шаг k звучит в момент
// timeAt(k/4) источника такта — с точностью до отсчёта. Chance, Ratchet и Swing (с p-lock). Сработавшие ноты играет
// сэмплер (SeqVoices) в своём кадре; в интерфейс уходят шаги и ноты.
// Не перенесено: запись стемов (StemRing, SequencerStems) — позже.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "ArrAutoPlay.h"
#include "Click.h"
#include "Launcher.h"
#include "PlayClock.h"
#include "SeqData.h"
#include "SeqOut.h"
#include "TempoSource.h"
#include "mod/ModEngine.h"
#include "sampler/VoiceParam.h"
#include "util/SPSCQueue.h"

namespace jambo {

/// Голоса, которые играют ноты секвенсора (Sampler в Swift-версии): граница через интерфейс — сэмплер переносится
/// отдельно.
class SeqVoices {
public:
    virtual ~SeqVoices() = default;
    /// Снимок звука колбэка (автоматизация пишет в него); nullptr — нет.
    virtual SoundData* sound() = 0;
    virtual void midiAllOff(int64_t at) = 0;
    virtual bool sendsMidi(int t) const = 0;
    virtual void sidechainTrig(int t, int64_t frame) = 0;
    virtual void midiNotes(int t, int64_t at, const int16_t* notes, int count, VoiceMask mask, const float* vals,
                           int64_t length, int liveId) = 0;
    virtual void trigger(int t, int64_t at, const int16_t* notes, int count, VoiceMask mask, const float* vals,
                         uint64_t hash, int64_t gate, bool own, int liveId) = 0;
};

class Sequencer {
public:
    Sequencer(double sampleRate, double bpm);

    SPSCQueue<SeqOut> out{4096, SeqOut()};
    /// Ход транспорта для интерфейса (запись нот с REC).
    PlayClock clock;
    bool playing() const { return playing_; }
    int tick() const { return tick_; }
    bool metronome = false;
    double metronomeGain() const { return click.gain; }
    void setMetronomeGain(double g) { click.gain = g; }
    const SeqData* data() const { return data_; }
    /// Новый снимок паттернов (без игры — активные ячейки и остановки из него).
    void setData(const SeqData* d);
    Launcher launcher;
    ArrAutoPlay autoPlay;
    /// Голоса (задаёт AudioCore).
    SeqVoices* sampler = nullptr;
    /// P-lock общих FX, LFO/ENV и громкостей MIX играющего шага (читают FxRack, ModEngine, микшер).
    std::vector<uint32_t> fxLockMask;
    std::vector<float> fxLockVal;
    std::vector<uint16_t> modLockMask;
    std::vector<float> modLockVal;
    uint16_t mixLockMask = 0;
    std::vector<float> mixLockGain;
    /// Модуляция — задаёт AudioCore.
    ModEngine* mod = nullptr;

    double bpm() const { return tempo.tempo(); }
    /// Темп: при игре — со следующего шага, без игры — сразу.
    void setBpm(double bpm);
    /// Старт и стоп в кадре at (первый шаг звучит в этом кадре).
    void setPlaying(bool on, int64_t at);
    /// Старт по MIDI-клоку: шаг 0 — в кадре frame.
    void start(int64_t frame, int64_t now);
    /// Ведомый по MIDI-клоку: шаг step должен звучать в кадре frame.
    void clockPhase(int step, double frame);
    void queue(int t, int k) {
        if (playing_) launcher.queue(t, k);
    }
    void adoptPlay();
    void queueScene(int k, bool atPass) {
        if (playing_) launcher.queueScene(k, atPass);
    }
    void setArrange(bool on, int from) {
        launcher.arrangeMode = on;
        launcher.arrFrom = std::max(0, from);
    }
    void seekArrange(int pos);
    void returnToArrange(int t);
    void leaveArrange() { launcher.setFollowAll(false); }
    /// Позиция курсора аранжировки в кадре frame (шаги, дробная; −1 — стоит).
    double arrangePosition(int64_t frame) const;
    /// Автоматизация на блок (до голосов).
    void applyAuto(SoundData& snd, int64_t frame);
    /// Кадр (дробный) шага k от старта.
    double stepFrame(int k) const { return tempo.timeAt(double(k) / TempoGrid::stepsPerBeat) * sampleRate; }
    /// Блок [blockStart, blockStart + frames): шаги, ноты, щелчки (щелчок прибавляется к left/right).
    void process(int64_t blockStart, int frames, float* left, float* right, double host = 0);

private:
    static constexpr int pendingMax = 512;
    double sampleRate;
    InternalTempoSource tempo;
    double wantedBpm;
    Click click;
    const SeqData* data_ = nullptr;
    bool playing_ = false;
    int tick_ = 0;
    std::vector<double> pendingTime;
    std::vector<int32_t> pendingTrack;
    std::vector<int16_t> pendingNotes;
    std::vector<int32_t> pendingCountN;
    std::vector<VoiceMask> pendingMask;
    std::vector<float> pendingVals;
    std::vector<uint64_t> pendingSound;
    std::vector<int64_t> pendingGate, pendingEnv;
    int pendingCount = 0;
    uint32_t rng = 0x9E3779B9u;

    void playStep(const SeqData& d, double time);
    void trig(const SeqData& d, int t, int i, int pos, double time, double dur);
    void schedule(int track, double time, const SeqData& d, int i, int64_t gate, int64_t envGate);
    void scheduleNote(int track, double time, const SeqData& d, int i, int j, int64_t gate, int64_t envGate);
    void movePending(int from, int to);
    void firePending(double end);
    float nextRandom();
};

}  // namespace jambo
