// Таймстретч NORMAL (RATE в TSTR; порт Core/Sampler/StretchPV.swift, ветка без Accelerate; ветка swift JAMbo): фазовый
// вокодер — кадры по 2048 отсчётов через 512, окно Ханна на анализе и синтезе (в сумме ×1,5). Фазовая привязка у пиков
// (Laroche–Dolson), атаки — фазы из анализа, стерео — фазы по середине, первый кадр ноты — без окна (атака с первого
// отсчёта), куски между атаками сэмпла (Onsets) тянутся отдельно. Кадры голосов разнесены по блокам колбэка (lead).
// Всё — в колбэке, без выделений памяти.
#pragma once
#include <cstdint>
#include <vector>
#include "PvFFT.h"
#include "PvVec.h"

namespace jambo {

class Vocoder {
public:
    static constexpr int size = 2048;
    static constexpr int hop = 512;
    static constexpr int bins = 1025;
    /// Кольцо выхода голоса: кадр (size) и запас на ранний расчёт (lead < hop).
    static constexpr int ring = 4096;
    static constexpr int ringMask = 4095;

    explicit Vocoder(int slots);

    /// Длина блока колбэка (ставит Sampler::render) — для разнесения кадров.
    int block = 256;
    /// Посчитано кадров (для тестов и замеров).
    int framesDone() const { return framesDone_; }
    std::vector<float> outL, outR;

    /// Новая нота голоса v: кольцо пустое, первый кадр — на первом отсчёте.
    void start(int v, double dir);
    /// m отсчётов голоса v в outL/outR (как Stretcher::fill); атаки — ons (nOns, по возрастанию); now — отсчёт колбэка
    /// первого из m.
    void fill(int v, const float* l, const float* r, int frames, bool stereo, double pos0, double speed, double ratio,
              int m, int skip, double lo, double hi, const double* ons, int nOns, int64_t now);

private:
    int slots;
    PvFFT fft;
    std::vector<float> win, winG, firstW, omg;
    float norm;
    int framesDone_ = 0;
    std::vector<float> ringL, ringR;
    std::vector<int> ringAt, hopLeft;
    std::vector<uint8_t> primed;
    std::vector<int> lead;
    std::vector<uint8_t> late;
    std::vector<int64_t> nextAt;
    std::vector<float> prevAna, prevSyn, prevMag, instF;
    std::vector<double> prevC;
    std::vector<uint8_t> prevHit;
    std::vector<double> lastDir, segLo, segHi;
    std::vector<float> fL, fR, reL, imL, reR, imR, reM, imM, mag, ph, syn, dc, ds;
    std::vector<int> peaks;
    PvCubic cub;

    int pickLead(int v, int64_t s);
    void segment(int v, double t, double lo, double hi, const double* ons, int n);
    void read(const float* p, int frames, double c, double step, double lo, double hi, float* out, bool raw);
    void analyze(bool stereo);
    void first(int v, const float* l, const float* r, int frames, bool stereo, double t0, double step, double lo,
               double hi);
    void frame(int v, const float* l, const float* r, int frames, bool stereo, double c, double step, double ratio,
               double lo, double hi, int d);
};

}  // namespace jambo
