// Голоса синта в колбэке (порт Core/Synth/SynthVoices.swift, SynthVoicesEnvelope.swift, SynthVoicesNotes.swift; ветка
// swift JAMbo): 6 голосов на дорожку, слот — по кругу (свободный: голос дозвучал), иначе крадётся самый тихий — гаснет
// за ~5 мс, новая нота — через 18 мс. Громкость — внешняя огибающая: Attack (не меньше 2 мс) → удержание → Decay как
// Release. Движок → огибающая → Level → фильтр → Pan → шина дорожки. Куски по 24 отсчёта.
// MULTICORE: дорожки блока — задания RenderPool (pool); без пула — по очереди.
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>
#include "JamboBank.h"
#include "LadderFilter.h"
#include "engine/RenderPool.h"
#include "mod/ModEngine.h"
#include "sampler/Biquad.h"
#include "sampler/SoundSnapshot.h"

namespace jambo {

class SynthVoices : public RenderJobs {
public:
    static constexpr int perTrack = 6;
    static constexpr int tracks = SoundData::maxTracks;
    static constexpr int slots = tracks * perTrack;
    static constexpr int chunk = 24;
    /// Голос без звука дольше 4800 отсчётов (и с начала ноты прошло больше) замолкает.
    static constexpr int silence = 4800;
    static constexpr int64_t forever = std::numeric_limits<int64_t>::max();

    explicit SynthVoices(double sampleRate);

    /// Сколько голосов звучит или ждёт старта.
    int activeCount() const;
    /// Ноты дорожки t движком e в кадре frame (gate: −1 — нота в 1 шаг, forever — живая).
    void trigger(const SoundData& d, int t, int e, int64_t frame, const int16_t* notes, int count, VoiceMask mask,
                 const float* vals, int64_t gate, bool isOwn, int live, bool retrig);
    void fade(int v, int64_t at, double tau);
    /// Голоса в шины дорожек (bus + t·stride); used[t] — шина уже очищена в этом блоке; посылы — (дорожка·3 + посыл)·stride.
    void render(const SoundData& d, int64_t blockStart, int n, float* busL, float* busR, float* sendL, float* sendR,
                uint8_t* used, int stride);
    /// Отпустили клавишу: живые ноты (liveId) — Decay как Release с кадра at.
    void release(int t, int id, int64_t at);
    /// Новый снимок: перестановка или удаление дорожек — голоса гаснут; переход MIX (rebind) — остаются.
    void adopt(const SoundData& d, int64_t now);
    int lastSerial() const { return serialCounter.load(std::memory_order_relaxed); }
    /// Держащиеся ноты дорожки t (не свой звук шага, созданные до serial last) — для перезапуска новым звуком.
    template <typename F>
    void retrigger(int t, int64_t now, int last, F&& body) {
        if (t < 0 || t >= tracks) return;
        const int64_t edgeTol = int64_t(0.01 * sr);
        const int base = t * perTrack;
        for (int v = base; v < base + perTrack; ++v) {
            if (!(active[v] != 0 && fading[v] == 0 && own[v] == 0 && serial[v] <= last)) continue;
            const int64_t at = std::max(now, start[v]);
            if (holdEnd[v] != forever && holdEnd[v] <= at + edgeTol) continue;
            const int64_t gate = holdEnd[v] == forever ? forever : holdEnd[v] - at;
            fade(v, at, 0.003);
            body(note[v], at, gate, lockMask[v], &lockVal[size_t(v * VoiceParam::count)], liveId[v]);
        }
    }

    /// Модуляция LFO/ENV (ставит AudioCore).
    ModEngine* mod = nullptr;
    /// Многоядерный режим (ставит AudioCore): дорожки блока — задания пула.
    RenderPool* pool = nullptr;
    /// Задание пула: голоса j-й дорожки блока.
    void runJob(int j) override;
    JamboBank bank;

private:
    double sr;
    std::vector<uint8_t> selfDecayE;
    std::vector<uint8_t> active, fading, own, released, triggered;
    std::vector<int> trackId, note, liveId, serial, engine;
    std::vector<int64_t> start, holdEnd, stopAt, fadeAt;
    std::vector<double> attackF, decaySec;
    std::vector<float> decayK, env, env0, fadeK, fadeG, level, send, pan;
    std::vector<double> hz, qDb;
    std::vector<int> ftype;
    std::vector<Biquad> coef;
    std::vector<float> z, zR, fdip;
    LadderFilter ladder;
    std::vector<int64_t> busyUntil;
    std::vector<uint8_t> selfDecay, keep;
    std::vector<int> quiet;
    std::vector<VoiceMask> lockMask;
    std::vector<float> lockVal;
    float* sendL = nullptr;
    float* sendR = nullptr;
    int stride = 0;
    int64_t modFrame = 0;
    std::vector<uint8_t> pActive;
    std::vector<int64_t> pStart;
    std::vector<int> pNote, pEngine;
    std::vector<int64_t> pGate;
    std::vector<uint8_t> pOwn, pKeep;
    std::vector<int> pLive;
    std::vector<VoiceMask> pMask;
    std::vector<float> pVal;
    std::vector<int> next;
    std::vector<JamboDrift> drift;
    int64_t blockStart_ = 0;
    std::atomic<int> serialCounter{0};
    std::vector<float> outBuf, outBufR, gBuf;
    std::vector<int> jobTracks;
    const SoundData* jobData = nullptr;
    int jobN = 0;
    float* jobBusL = nullptr;
    float* jobBusR = nullptr;

    double pv(const SoundData& d, int t, int p, VoiceMask mask, const float* vals) const;
    void setup(int v, const SoundData& d, int t, int e, int64_t frame, int n, int64_t gate, VoiceMask mask,
               const float* vals, bool isOwn, int live, bool steal);
    void activatePending(int v, const SoundData& d);
    void renderTrack(int t, const SoundData& d, int n, float* busL, float* busR);
    void renderSlot(int v, const SoundData& d, int t, int64_t blockStart, int n, float* bl, float* br);
    double knob(const float* row, const float* locks, VoiceMask mask, const ModEngine* modE, int t, int64_t frame,
                int p) const;
    void renderChunk(int v, const SoundData& d, int t, int64_t frame0, int m, float* bl, float* br, int off);
    /// Огибающая куска в gBuf; голос остановился — true.
    bool envelope(int v, int64_t frame0, int m, float lv);
    struct Slot {
        int v;
        bool steal;
    };
    Slot allocate(int t, int64_t at);
    void legatoCut(int t, const int16_t* notes, int count, int64_t at, double attack);
};

}  // namespace jambo
