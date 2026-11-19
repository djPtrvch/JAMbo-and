// Room (Headphone Monitor, фаза 2; порт Core/Monitor/RoomConv.swift, ветка swift JAMbo): свёртка 2×2 — равномерное
// разбиение на партиции по 128 отсчётов (БПФ 256, overlap-save), задержка 128 отсчётов; прямая часть и хвост — в разных
// сумматорах (выход = прямая + Ambience · хвост). HEAD — спектры прямой части по углу поворота головы. Срез низа колонок —
// фильтр на входе. RoomStage — в колбэке: ФИФО 128, смена комнаты — кроссфейд 40 мс.
// Отличие от Swift: экземпляры передаются сырыми указателями через очереди (в Swift — Unmanaged); освобождает основной
// поток (drain) и деструктор RoomStage.
#pragma once
#include <memory>
#include <optional>
#include <vector>
#include "RoomIR.h"
#include "sampler/PvFFT.h"
#include "util/AtomicFloat.h"
#include "util/SPSCQueue.h"

namespace jambo {

class RoomConv {
public:
    static constexpr int part = 128, size = 256, bins = 129;
    /// head — набор HEAD (4 пути, наборы по part кадров подряд), from и step — углы, градусы.
    explicit RoomConv(const RoomIR& ir, const RoomIR* head = nullptr, double from = -50, double step = 2);
    const std::string name;
    /// Основной поток: срез низа колонок — частота и порядок (2 или 4; 0 — нет).
    void setHighpass(double hz, int order, double sampleRate);
    /// Колбэк: история входа из прежнего экземпляра той же длины (false — другая длина).
    bool takeState(const RoomConv& o);
    /// Колбэк: отсчёт входа на место k (через срез низа).
    void feed(int k, float l, float r);
    /// Блок: новые 128 отсчётов — в inL[128…], inR[128…]; выход — out (4 × 128).
    void step(float yaw = 0, bool headOn = false);
    std::vector<float> out;

private:
    int parts, directParts;
    PvFFT fft;
    std::vector<float> hr, hi, xr, xi;
    int head = 0;
    std::vector<float> ar, ai, inL, inR, tmp;
    int headCount;
    float headFrom, headStep;
    std::vector<float> headR, headI, hpR, hpI, hnR, hnI, aoR, aoI;
    bool headActive = false;
    double hpc[10] = {}, hps[8] = {};
    int hpN = 0;

    void copyStatic(float* dr, float* di);
    void headSpectra(float yaw);
    void blendDirect(int h);
    static void cmac(const float* xr, const float* xi, const float* hr, const float* hi, float* ar, float* ai, int n);
};

class RoomStage {
public:
    static constexpr float fadeFrames = 1920;
    static constexpr float yawGlide = 0.2835f;
    explicit RoomStage(double sampleRate);
    ~RoomStage();
    RoomStage(const RoomStage&) = delete;
    RoomStage& operator=(const RoomStage&) = delete;

    AtomicFloat ambience{0.5f};
    AtomicFloat headYaw{0}, headOn{0};
    /// Колбэк: комната звучит или идёт переход.
    bool active() const { return cur != nullptr || fading || hasPending; }
    /// Основной поток: новая комната (nullptr — Off).
    void set(std::unique_ptr<RoomConv> conv);
    /// Основной поток: освободить отработавшие экземпляры.
    void drain();
    /// Колбэк: блок на месте.
    void process(double* l, double* r, int n);

private:
    SPSCQueue<RoomConv*> incoming{16, nullptr};
    SPSCQueue<RoomConv*> retired{64, nullptr};
    float yaw = 0;
    RoomConv* cur = nullptr;
    RoomConv* next = nullptr;
    RoomConv* pending = nullptr;
    bool hasPending = false;
    bool fading = false;
    float fade = 0;
    float amb = 0.5f;
    int fill = 0;
    float glide;

    void retire(RoomConv* u) {
        if (u) retired.push(u);
    }
};

}  // namespace jambo
