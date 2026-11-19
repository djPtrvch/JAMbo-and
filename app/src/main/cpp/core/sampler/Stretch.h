// Таймстретч голоса сэмплера BEAT (RATE в TSTR; порт Core/Sampler/Stretch.swift, ветка swift JAMbo): WSOLA — зёрна по
// 2048 отсчётов через 1024, окно Ханна. Указатель времени идёт со скоростью темпа и RATE, внутри зерна сэмпл читается со
// скоростью высоты. Новое зерно ищется возле указателя (±288 отсчётов) по нормированной корреляции (96 точек через 4).
// Первое зерно ноты — без нарастания. Всё — в колбэке, без выделений памяти.
#pragma once
#include <cstdint>
#include <vector>
#include "PvVec.h"

namespace jambo {

class Stretcher {
public:
    static constexpr int grain = 2048;
    static constexpr int hop = 1024;
    static constexpr int search = 288;
    static constexpr int searchStep = 4;
    static constexpr int corrPoints = 96;
    static constexpr int corrStep = 4;
    /// Кусок голоса за один вызов fill.
    static constexpr int maxChunk = 32;

    explicit Stretcher(int slots);

    /// Новая нота голоса v: зёрен нет, первое начнётся на первом отсчёте.
    void start(int v, double dir);
    /// m отсчётов голоса v в outL/outR: первые skip — тишина; указатель времени от pos0 со скоростью speed, чтение
    /// внутри зерна — со скоростью ratio по направлению скорости; зёрна начинаются в пределах lo…hi.
    void fill(int v, const float* l, const float* r, int frames, bool stereo, double pos0, double speed, double ratio,
              int m, int skip, double lo, double hi);
    static float cubic(const float* p, int n, double x);

    std::vector<float> outL, outR;

private:
    std::vector<float> window;
    std::vector<double> gPos;
    std::vector<int> gAge;
    std::vector<uint8_t> gFirst;
    std::vector<double> gDir;
    std::vector<int> phase;
    std::vector<double> lastDir;
    std::vector<float> target;
    std::vector<double> offsets;
    std::vector<int> pIdx;
    std::vector<float> pRel, pVal;
    PvCubic cub;
    std::vector<float> tmp;

    double place(const float* l, int frames, double base, double step, int prev, double lo, double hi);
    void grainAdd(const float* p, int frames, double x0, double step, int n, float* out, const float* w);
};

}  // namespace jambo
