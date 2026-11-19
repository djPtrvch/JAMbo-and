// Снимок звука для колбэка (порт Core/Sampler/SoundSnapshot.swift, ветка swift JAMbo): ручки дорожек, громкость
// микшера, mute/solo, сэмплы дорожек с маркерами нарезки, MOD, FX, мастер, EQ. Строится в основном потоке после каждой
// правки; держит ссылки на сэмплы и таблицы, пока колбэк может их читать. Массивы выделены при построении — колбэк
// только читает.
// Не перенесено (с таймстретчем и WARP): темп файла (eBpm — 0), атаки (eOnsets — нет), метки варпинга.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "SampleData.h"
#include "VoiceParam.h"
#include "eq/EqModel.h"
#include "master/MasterChain.h"
#include "mod/ParamScale.h"
#include "model/Sounds.h"
#include "synth/GranularVoice.h"
#include "synth/Wavetable.h"

namespace jambo {

struct Project;

struct SoundData {
    static constexpr int maxTracks = Params::maxTracks;
    static constexpr int maxEntries = 1024;
    static constexpr int maxSlices = 8192;

    int trackCount = 0;
    /// Переход MIX: голоса переходят к новым id по месту.
    bool rebind = false;
    /// Второй звук дорожки t (клип altSlot[t]) — на номере t + maxTracks; какой действует — по slotRef (Launcher).
    std::vector<uint8_t> alt;
    std::vector<int> altSlot;
    const int* slotRef = nullptr;
    int entryTotal = 0;
    /// Сэмплы предпрослушки библиотеки: записи extraStart ..< extraStart + extraCount.
    int extraStart = 0, extraCount = 0;
    std::vector<int> trackId;
    std::vector<float> params, gain, trim;
    std::vector<uint8_t> open, midiOut;
    std::vector<uint64_t> sound;
    std::vector<int32_t> entryStart, entryCount;
    std::vector<uint64_t> eHash;
    std::vector<const float*> eLeft, eRight;
    std::vector<int> eFrames;
    std::vector<int32_t> eChannels, eSliceStart, eSliceCount;
    std::vector<double> eBpm;
    std::vector<const double*> eOnsets;
    std::vector<int> eOnsetCount;
    std::vector<double> sliceAt;
    std::vector<int32_t> sliceNote;
    std::vector<WtRef> tableRef;
    std::vector<GrainSrc> grainRef;
    /// Общие FX (Params::fxIds), FX клипов ((дорожка · patternSlots + клип) · fxCount), глубина Sidechain, переключатели
    /// SETTINGS → SIDECHAIN.
    std::vector<float> fx, clipFx, scAmount, scSend;
    /// MOD: параметры LFO/ENV, LFO по темпу, режим RATE, слежение ударных за нотами, алгоритм таймстретча.
    std::vector<float> mod;
    std::vector<uint8_t> lfoSync, rateMode, pitchKey;
    uint8_t stretchAlgo = 1;
    std::vector<int8_t> asSrc;
    std::vector<float> asAmt;
    std::vector<ParamScale> scale;
    std::vector<uint8_t> asAny;
    std::vector<int8_t> fxSrc;
    std::vector<float> fxAmt;
    std::vector<int32_t> fxTrack;
    std::vector<ParamScale> fxScale;
    /// Мастер — в порядке MasterChain::Index.
    std::vector<double> master;
    /// EQ каналов (EqSpec::fill), по EqSpec::stride значений.
    std::vector<double> eq;
    std::vector<int> eqOrder;
    std::vector<uint8_t> eqDep;
    /// Референс аранжировщика.
    const float* refLeft = nullptr;
    const float* refRight = nullptr;
    int refFrames = 0;
    double refOffset = 0;
    float refGain = 1;
    bool refB = false;

    /// Номер звука дорожки t: t или t + maxTracks (играет клип altSlot со вторым звуком).
    int s(int t) const { return alt[size_t(t)] != 0 && slotRef[t] == altSlot[size_t(t)] ? t + maxTracks : t; }
    /// Параметры EQ дорожки t (второй звук — за каналами EqSpec::channels).
    const double* eqParams(int t) const {
        const int u = s(t);
        const int ch = u == t ? t : EqSpec::channels + t;
        return eq.data() + size_t(ch) * EqSpec::stride;
    }
    float param(int t, int p) const { return params[size_t(s(t) * VoiceParam::count + p)]; }
    /// Сэмпл дорожки t с ключом hash (−1 — нет).
    int entry(int t, uint64_t hash) const;
    /// Сэмпл с ключом hash на любой дорожке (предпрослушка).
    int anyEntry(uint64_t hash) const;
    /// Есть ли в снимке буфер data.
    bool holds(const float* data) const;
};

class SoundSnapshot {
public:
    struct Options {
        SliceMap map = SliceMap::chromatic;
        int octave = 3;
        std::vector<int> octaves;
        std::map<std::string, std::shared_ptr<WtTable>> tables;
        SidechainSends scSends;
        bool levelRelative = true;
        bool stretchBeat = false;
        /// Активные ячейки дорожек в колбэке (Launcher::slot; nullptr — свой буфер, все на первой ячейке).
        const int* slots = nullptr;
        /// Параметры мастера кроссфейдера перехода MIX (пусто — из проекта).
        std::vector<double> master;
        std::vector<std::string> extra;
        std::vector<int> next;
        std::map<int, int> keepAlt;
        bool rebind = false;
        bool refB = false;
        bool warpFollow = false;
    };

    SoundSnapshot(const Project& project, const std::map<std::string, SampleRef>& samples, const Options& o);
    SoundSnapshot(const SoundSnapshot&) = delete;
    SoundSnapshot& operator=(const SoundSnapshot&) = delete;
    ~SoundSnapshot();

    const SoundData& data() const { return data_; }
    /// Ячейки второго звука (id дорожки → ячейка) — keepAlt следующего снимка.
    const std::map<int, int>& altKeep() const { return altKeep_; }
    static int liveCount;

private:
    SoundData data_;
    std::vector<SampleRef> keep;
    std::vector<std::shared_ptr<WtTable>> keepTables;
    std::vector<int> ownSlots;
    std::map<int, int> altKeep_;

    void fillClipFx(const Project& project, int tracks);
};

}  // namespace jambo
