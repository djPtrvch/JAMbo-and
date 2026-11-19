// Уровни для измерителя и метров MIX (порт Core/Engine/Levels.swift, ветка swift JAMbo): колбэк пишет пики каждого блока
// — дорожки (после громкости микшера и Sidechain, до mute/solo), выход мастера, референс — и приглушение Sidechain;
// интерфейс читает новые блоки по таймеру. LUFS — Loudness (BS.1770-4 / EBU R128).
#pragma once
#include <functional>
#include <limits>
#include <vector>
#include "TrackTaps.h"
#include "sampler/SoundSnapshot.h"
#include "util/AtomicFloat.h"

namespace jambo {

/// Громкость показанного канала: K-взвешивание; Momentary (400 мс, раз в 100 мс), Short-term (3 с), Integrated (пороги
/// −70 LUFS и −10 LU, корзины по 0,1 LU).
class Loudness {
public:
    static constexpr int window = 19200;
    static constexpr int shortWindow = 144000;
    static constexpr int hop = 4800;
    static constexpr int bins = 800;
    static constexpr double absGate = -70.0;
    static constexpr int ringBlocks = 2048;
    static constexpr int maxFrames = 4096;

    explicit Loudness(double sampleRate);
    /// Что слушать: дорожка, Levels::master; −1 — ничего.
    AtomicCounter source, resetRequests;
    AtomicFloat momentary{-std::numeric_limits<float>::infinity()};
    AtomicFloat integrated{-std::numeric_limits<float>::infinity()};
    AtomicFloat shortTerm{-std::numeric_limits<float>::infinity()};

    /// Колбэк: слушает ли громкость канал ch (заодно — смена канала и сброс).
    bool listens(int ch);
    /// Колбэк: n отсчётов показанного канала.
    void process(const float* l, const float* r, int n);
    /// Колбэк: конец блока; если канал молчал — тишина.
    void end(int n);
    /// Интегральная громкость файла (вне колбэка).
    static double integratedOf(const float* left, const float* right, int frames);

private:
    double c[10];
    double z[16] = {};
    std::vector<double> energy;
    std::vector<int> frames;
    std::vector<double> binN, binE;
    std::vector<float> zeros;
    long written = 0;
    int sinceHop = 0;
    bool fed = false;
    long lastSource = -1;
    long lastReset = 0;

    void restart();
    void clearIntegrated();
    double filter(const float* x, double* s, int n);
    double filterSilence(double* s, int n);
    void push(double e, int n);
    double mean(int span) const;
    void integrate(double e);
};

class Levels {
public:
    static constexpr int master = SoundData::maxTracks;
    static constexpr int ref = SoundData::maxTracks + 1;
    static constexpr int channels = SoundData::maxTracks + 2;
    static constexpr int stride = 3;
    static constexpr int ring = 64;

    Levels();
    AtomicCounter blocks;
    Loudness loudness{48000};
    Loudness mixLoudness{48000};
    TrackTaps trackTaps;

    /// Колбэк: значения текущего блока — с нуля.
    float* begin();
    /// Колбэк: значения канала ch в текущем блоке.
    float* current(int ch) { return data.data() + size_t(((blocks.load() % ring) * channels + ch) * stride); }
    /// Колбэк: блок записан.
    void commit(int frames);
    /// Интерфейс: блоки после since — f(значения блока по каналам). Возвращает новый счётчик.
    long read(long since, const std::function<void(const float*)>& f) const;
    static float value(const float* block, int ch, int k) { return block[ch * stride + k]; }

private:
    std::vector<float> data;
};

}  // namespace jambo
