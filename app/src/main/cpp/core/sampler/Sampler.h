// Голоса сэмплера и микшер дорожек в колбэке (порт Core/Sampler/Sampler.swift, SamplerNotes, SamplerMix, SamplerMidi,
// SamplerPreview, SoundWarp, Core/Synth/SamplerSynth.swift; ветка swift JAMbo).
// Голос: сэмпл (Start/End или маркер нарезки, кубическая интерполяция, край — 4 мс) → Attack/Decay (длинная нота держит
// уровень до конца, Decay — как Release) → Velocity → фильтр (LP/HP + Resonance) → Pan → шина дорожки → EQ → громкость
// микшера и гейт mute/solo → выход. RATE: PTCH (скорость и высота) или TSTR (таймстретч BEAT — Stretcher, NORMAL —
// Vocoder; WARP). Без выделений памяти после создания.
// EQ дорожек, метры, спектры и замеры колбэка — SamplerHooks.cpp (в Swift — TrackEqs.swift и микшер).
#pragma once
#include <cstdint>
#include <limits>
#include <vector>
#include "Biquad.h"
#include "NoteLocks.h"
#include "SoundSnapshot.h"
#include "Stretch.h"
#include "StretchPV.h"
#include "VoiceParam.h"
#include "engine/RenderPool.h"
#include "engine/StemRing.h"
#include "fx/FxRack.h"
#include "mod/ModEngine.h"
#include "sequencer/Sequencer.h"
#include "synth/SynthVoices.h"
#include "util/SPSCQueue.h"

namespace jambo {

class TrackEqs;
class EqDynLevels;
class EqListen;
class Spectra;
class Levels;
class RenderProfile;

/// Из колбэка в интерфейс: полоса воспроизведения.
struct VoiceOut {
    enum class Kind : uint8_t { none, start, end };
    Kind kind = Kind::none;
    int voice = 0;
    /// −1 — предпрослушка.
    int track = 0;
    uint64_t hash = 0;
    int64_t frame = 0;
    /// Отрезок a…b (доли), скорость, длина сэмпла.
    double a = 0, b = 0, rate = 0;
    int frames = 0;

    static VoiceOut start(int v, int t, uint64_t h, int64_t f, double a, double b, double rate, int frames) {
        return {Kind::start, v, t, h, f, a, b, rate, frames};
    }
    static VoiceOut end(int v) { return {Kind::end, v, 0, 0, 0, 0, 0, 0, 0}; }
};

/// Нота для MIDI OUT: Note On (длина в кадрах; −1 — живая, до отпускания liveId), отпускание живой или «всё отпустить».
struct MidiNoteOut {
    static constexpr uint8_t on = 1;
    static constexpr uint8_t release = 2;
    static constexpr uint8_t allOff = 3;
    uint8_t kind = 0;
    int32_t track = 0;
    int16_t note = 0;
    uint8_t velocity = 0;
    int64_t frame = 0;
    int64_t length = 0;
    int liveId = 0;
};

class Sampler : public SeqVoices, public RenderJobs {
public:
    /// Больше 48 голосов — самый старый гаснет за ~5 мс; ячеек с запасом на гаснущие.
    static constexpr int maxVoices = 48;
    static constexpr int slots = 64;
    static constexpr int maxFrames = 4096;
    static constexpr float previewGain = 0.7f;
    static constexpr int previewVoice = 99;
    static constexpr int warpCap = 1024;
    static constexpr int warpLive = -9;
    static constexpr int64_t forever = std::numeric_limits<int64_t>::max();

    explicit Sampler(double sampleRate);

    SPSCQueue<VoiceOut> out{2048, VoiceOut()};
    SPSCQueue<MidiNoteOut> midiOut{2048, MidiNoteOut()};

    /// Голоса синта (ставит AudioCore); звук «mi:<n>» — ноты уходят туда.
    SynthVoices* synth = nullptr;
    FxRack* fx = nullptr;
    TrackEqs* eqs = nullptr;
    RenderPool* pool = nullptr;
    EqDynLevels* eqDyn = nullptr;
    EqListen* eqListen = nullptr;
    Spectra* spectra = nullptr;
    Levels* levels = nullptr;
    RenderProfile* profile = nullptr;
    ModEngine* mod = nullptr;
    /// P-lock громкостей играющего шага MIX (ставит AudioCore).
    const uint16_t* mixLockMask = nullptr;
    const float* mixLockGain = nullptr;
    /// Запись стемов: гейт mute/solo по отсчётам (дорожка·maxFrames) — пишет микшер; nullptr — запись не идёт.
    float* stemGate = nullptr;
    /// Темп (время Delay, TSTR) — ставит AudioCore на блок.
    double bpm = 120;
    /// Выходы дорожек после громкости микшера, до mute (дорожка·maxFrames).
    std::vector<float> outL, outR;
    Stretcher stretch{slots};
    Vocoder vocoder{slots};

    int activeCount() const;
    bool previewing() const { return pvL != nullptr; }

    // SeqVoices.
    SoundData* sound() override { return sound_; }
    void midiAllOff(int64_t at) override;
    bool sendsMidi(int t) const override;
    void sidechainTrig(int t, int64_t frame) override;
    void midiNotes(int t, int64_t at, const int16_t* notes, int count, VoiceMask mask, const float* vals,
                   int64_t length, int liveId) override;
    void trigger(int t, int64_t at, const int16_t* notes, int count, VoiceMask mask, const float* vals, uint64_t hash,
                 int64_t gate, bool own, int liveId) override {
        triggerNotes(t, at, notes, count, mask, vals, hash, gate, own, liveId, false);
    }

    /// Ноты дорожки t в кадре frame. hash — свой звук шага (0 — звук дорожки); gate — удержание в кадрах (−1 — нота в 1
    /// шаг; forever — живая нота до release).
    void triggerNotes(int t, int64_t frame, const int16_t* notes, int count, VoiceMask mask, const float* vals,
                      uint64_t hash, int64_t gate, bool isOwn, int live, bool retrig);
    /// Блок: голоса в шины дорожек, микшер (громкость, mute/solo) — прибавляются к left/right.
    void render(int64_t blockStart, int frames, float* left, float* right);
    /// Отпустили клавишу: живые ноты (liveId) — Decay как Release с кадра at.
    void release(int t, int id, int64_t at);
    /// Живая нота с клавиатуры: держится до release; velocity — сила нажатия (1 — без p-lock).
    void noteOn(int t, int n, int id, float velocity, int64_t at);
    /// Снимок звука сменился (перестановка, переход MIX, смена звука дорожки).
    void adopt(SoundData& d, int64_t now);
    void midiRelease(int live, int64_t frame);

    /// ▶ редактора: сэмпл hash от a до b (доли) в исходной высоте сразу на выход; новая обрывает прошлую.
    void preview(uint64_t hash, double a, double b, int64_t at);
    void stopPreview(int64_t at = 0);
    void renderPreview(int n, float* left, float* right);
    /// ▶ пресета: нота дорожки t звуком hash с p-lock ручек пресета, ~0,5 с.
    void presetNote(int t, int n, uint64_t hash, const NoteLocks& locks, int64_t at);
    /// ▶ WARP: сэмпл hash дорожки t целиком, нота в исходной высоте, до конца сэмпла или стопа.
    void warpPreview(int t, uint64_t hash, int64_t at);

    void runJob(int j) override;

private:
    SoundData* sound_ = nullptr;
    std::vector<uint64_t> synthHash;
    double sr;

    // Голоса (по ячейке).
    std::vector<uint8_t> active, fading, own;
    std::vector<int> track, trackId, note, liveId, serial;
    std::vector<const float*> srcL, srcR;
    std::vector<int> srcFrames;
    std::vector<uint8_t> stereo;
    std::vector<int64_t> start;
    std::vector<double> pos, endPos, beginPos, edge, noteRatio, rate;
    std::vector<uint8_t> tstr;
    std::vector<double> grainRatio, srcBpm;
    std::vector<const double*> onsetAt;
    std::vector<int> onsetCount;
    std::vector<int> warpN;
    std::vector<double> warpU, warpP, uPos;
    std::vector<double> attackF, decaySec;
    std::vector<float> decayK;
    std::vector<int64_t> holdEnd;
    std::vector<uint8_t> released;
    std::vector<float> env;
    std::vector<int64_t> stopAt, fadeAt;
    std::vector<float> fadeK, fadeG, level, pan, send;
    std::vector<double> hz, qDb;
    std::vector<int> ftype;
    std::vector<Biquad> coef;
    std::vector<float> z, fdip;
    std::vector<VoiceMask> lockMask;
    std::vector<float> lockVal;
    int nextSerial = 0;

    // Шины дорожек, микшер.
    std::vector<float> busL, busR;
    std::vector<uint8_t> used;
    std::vector<float> volCur, gateCur;
    std::vector<float> sendL, sendR;
    std::vector<float> duckCur, flagCur;
    std::vector<int> eqJobs;
    std::vector<uint8_t> eqDone;
    const SoundData* eqJobData = nullptr;
    int eqJobN = 0;
    bool eqJobSends = false;
    std::vector<float> meterL, meterR;
    int64_t modFrame = 0;

    // Предпрослушка.
    const float* pvL = nullptr;
    const float* pvR = nullptr;
    double pvPos = 0, pvEnd = 0;
    float pvGain = 0;
    bool pvStop = false;
    bool pvStereo = false;
    bool warpPv = false;

    int16_t one[1] = {0};
    float scratch[VoiceParam::count] = {};

    static float cubic(const float* p, int n, double x);
    void clearSends(int t, int n);
    void renderVoice(int v, const SoundData& d, int64_t blockStart, int n);
    // Ноты (SamplerNotes.cpp).
    double pv(const SoundData& d, int t, int p, VoiceMask mask, const float* vals) const;
    void setupVoice(int v, const SoundData& d, int t, int e, int64_t frame, double a, double b, double ratio, int n,
                    double attack, double decay, int64_t gate, VoiceMask mask, const float* vals);
    int allocate(int64_t frame);
    void fade(int v, int64_t at, double tau);
    void finish(int v);
    void legatoCut(int t, const int16_t* notes, int count, int64_t at, double attack);
    static int indexOf(int id, const SoundData& d);
    void moveSynthNotes(const SoundData& o, const SoundData& d, int64_t now);
    void retrigger(int t, int64_t now);
    // Синт (SamplerSynth.cpp).
    int synthEngine(uint64_t h) const;
    void retriggerSynth(int t, int64_t now, int last);
    // WARP (SamplerWarp.cpp).
    double warpPos(int v, double x) const;
    double warpInv(int v, double p) const;
    void setupWarp(int v, const SoundData& d, int e, bool ts);
    double warpChunk(int v, double rate, int m);
    // Микшер (SamplerMix.cpp).
    void mix(const SoundData& d, int n, float* left, float* right);
    void parallelEqs(const SoundData& d, int n, bool sends);
    // EQ, метры и замеры (SamplerHooks.cpp).
    void wakeEqs(const SoundData& d, int n);
    void idleEq(int t, const SoundData& d, int n);
    void trackEq(int t, const SoundData& d, int n, bool sends);
    void lap(int section);
    float* meterOf(int t);
    bool loudOf(int t);
    void loudProcess(const float* l, const float* r, int n);
    void tapTracks(const SoundData& d, int n);
};

}  // namespace jambo
