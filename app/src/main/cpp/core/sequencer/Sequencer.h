// Секвенсор в аудиоколбэке (порт Core/Sequencer/Sequencer.swift, ветка swift JAMbo): шаг k звучит в момент
// timeAt(k/4) источника такта — с точностью до отсчёта. Chance, Ratchet и Swing (с p-lock). Сработавшие ноты играет
// сэмплер (SeqVoices) в своём кадре; в интерфейс уходят шаги и ноты.
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
#include "engine/StemRing.h"
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
    /// Запись стемов: звук — в кольцо, правила начала и конца — SequencerStems.cpp.
    StemRing stemRing;

    // Стемы (SequencerStems.swift): SequencerStems.cpp
    /// Готовность канала к записи (кнопка ● горит).
    void armStem(int ch, bool on);
    /// Тап по ● во время записи: дописать до конца прохода, готовность снимается.
    void finishStem(int ch);
    /// PLAY во время записи: дописать и остановиться; false — ничего не пишется (остановить сразу).
    bool requestStemStop();

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
    /// Состояние правил записи стемов (только колбэк; каналы — как у StemRing).
    struct StemRules {
        std::vector<uint8_t> armed = std::vector<uint8_t>(StemRing::channels, 0);
        std::vector<uint8_t> rec = std::vector<uint8_t>(StemRing::channels, 0);
        std::vector<uint8_t> finish = std::vector<uint8_t>(StemRing::channels, 0);
        std::vector<int> startTick = std::vector<int>(StemRing::channels, 0);
        std::vector<int64_t> lastEnd = std::vector<int64_t>(StemRing::channels, -1);
        /// Клипы (биты ячеек), уже записанные в стем дорожки; у мастера — клипы каждой дорожки.
        std::vector<uint16_t> seen = std::vector<uint16_t>(SeqData::maxTracks, 0);
        std::vector<uint16_t> masterSeen = std::vector<uint16_t>(SeqData::maxTracks, 0);
        bool stopAsked = false;
        bool anyRecording() const {
            for (uint8_t r : rec) if (r) return true;
            return false;
        }
    };
    StemRules stemRules;
    /// Шаг в кадре time; true — пора остановить транспорт.
    bool stemsStep(const SeqData& d, double time);
    /// Транспорт остановился: всё, что пишется, кончается в кадре at.
    void stemsStopped(int64_t at);
    void stemsStart(const SeqData& d, int64_t at);
    void stemBegin(int ch, int64_t at);
    void stemEnd(int ch, int64_t at);
    bool stemsArranged(const SeqData& d) const;
    bool stemsAnyPlaying(const SeqData& d) const;
    void stemsMasterSee(const SeqData& d);
    bool stemsMasterAllSeen(const SeqData& d) const;
    bool stemsMasterEdge(const SeqData& d, int k) const;
    void stemsPredict(const SeqData& d, bool arranged);
    int stemsTrackEndTick(const SeqData& d, int t, bool arranged) const;
    int stemsMasterEndTick(const SeqData& d, bool arranged) const;

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
