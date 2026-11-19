// GRANULAR (26): облако зёрен из сэмпла дорожки. Порт 1:1 Core/Jambo/GranularVoice.swift (ветка swift JAMbo);
// поведение — docs/ai/synth-engines.md JAMbo. Полифоничен: голос на слот; дрейф позиции, счёт звучащих нот и лимит
// зёрен — общие на дорожку (GrainTrack). Ручки 0…1: 1.1 POSITION, 1.2 SIZE, 1.3 DENSITY, 1.4 SPRAY.
// В колбэке памяти не выделяет.
#pragma once
#include <cstdint>
#include <functional>

namespace jambo {

/// Сэмпл GRANULAR для колбэка — только указатели; period — период источника, с (0 — неизвестен).
struct GrainSrc {
    const float* left = nullptr;
    const float* right = nullptr;
    int frames = 0;
    int channels = 1;
    double period = 0;
};

/// Общее на дорожку GRANULAR: дрейф позиции, звучащие ноты, зёрна (лимит), последний курсор.
struct GrainTrack {
    int64_t rnd = 1;
    double dr = 0, drT = 0, drHold = 0;
    int notes = 0;
    int grains = 0;
    double head = 0;

    double r2();
    /// Дрейф за dt секунд (новая цель каждые 1,5–4,5 с).
    void advance(double dt);
};

class GranularVoice {
public:
    static constexpr int maxGrains = 240;
    static constexpr double root = 48.0;
    static constexpr int winPoints = 128;
    static constexpr int maxChunk = 256;

    GranularVoice(double sampleRate, int seed = 1, GrainTrack* track = nullptr);
    ~GranularVoice();
    GranularVoice(const GranularVoice&) = delete;
    GranularVoice& operator=(const GranularVoice&) = delete;

    /// Окно w(x), x = 0…1 — по таблице.
    static double window(double x, bool gauss);

    void set(double note, double q1, double q2, double q3, double q4);
    /// Нота: поток зёрен не шёл — первое зерно через 3 мс; уже идёт — продолжается.
    void trigger(double note, double q1, double q2, double q3, double q4);
    /// Голос молчал: зёрен нет, усиление встаёт сразу.
    void silence();
    /// Стерео-выход n кадров (n ≤ maxChunk); window — кадров окна запуска.
    void render(float* outL, float* outR, int n, int window, const GrainSrc& src);

    double head() const { return head_; }
    int grains() const { return grains_; }
    bool spawning() const { return spawning_; }
    /// Запуск зерна (тесты): момент, позиция, высота, панорама; в приложении пусто.
    std::function<void(double, double, double, double)> onSpawn;

private:
    double r2();
    void spawn(double start, const GrainSrc& src, double dur, bool gauss);

    double sr_;
    GrainTrack* track_;
    bool ownsTrack_;
    double note_ = 48;
    double position_ = 0.08, size_ = 0.62, density_ = 0.6, spray_ = 0.25;
    double sizeS_ = 0.62, densS_ = 0.6;
    int64_t rnd_ = 1;
    double head_ = 0.08;
    double gain_ = 0;
    bool fresh_ = true;
    double gPos_[maxGrains] = {}, gStep_[maxGrains] = {}, gPh_[maxGrains] = {}, gPhStep_[maxGrains] = {};
    uint8_t gGauss_[maxGrains] = {};
    double gCr_[maxGrains], gCi_[maxGrains] = {}, gWr_[maxGrains], gWi_[maxGrains] = {};
    int gAt_[maxGrains] = {};
    double gG_[maxGrains * 4] = {};
    GrainSrc gSrc_[maxGrains];
    int grains_ = 0;
    double longest_ = 0;
    double nextAt_ = 0;
    bool spawning_ = false;
    double busL_[maxChunk] = {}, busR_[maxChunk] = {};
    double clock_ = 0;
};

}  // namespace jambo
