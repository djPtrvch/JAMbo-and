// Reverb: гибрид пластины и зала, стерео вход и выход. Порт 1:1 Core/FX/Reverb.swift (ветка swift JAMbo);
// поведение — docs/ai/fx-eq.md JAMbo. Совпадает с эталонами CoreTests/golden/fx_rev_*.i16 по отсчётам.
// Параметры — k-rate: size (0–100), decay (с), tone (−100…100), lowcut (Гц), predelay (мс).
#pragma once
#include <cstdint>
#include <vector>

namespace jambo {

class FxReverb {
public:
    /// seed — генератор фаз LFO линий.
    FxReverb(double sampleRate, uint32_t seed);

    /// Блок n отсчётов: вход (nullptr — тишина) → outL/outR (перезаписывает).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                 float size, float decay, float tone, float lowcut, float predelay);
    void reset();
    /// Покой: линии и фильтры с нуля, ручки при пробуждении — сразу целевые.
    void sleep();

private:
    double scaleOf(double size) const;
    double toneR(double t) const;
    void updateLoop();
    void updateFilters();

    double sr_;
    bool started_ = false;
    double size_ = 50, decay_ = 2, tone_ = 0, lowcut_ = 20;
    double preMs_;
    double scale_;
    // Биквады: [b0, b1, b2, a1, a2] и состояния [x1, x2, y1, y2] × 2 канала.
    double kHp_[5] = {}, zHp_[8] = {}, kLp_[5] = {}, zLp_[8] = {}, kSh_[5] = {}, zSh_[8] = {};
    double coefSig0_, coefSig1_;
    int preLen_;
    std::vector<float> preL_, preR_;
    int preW_ = 0;
    int erLen_;
    std::vector<float> erL_, erR_;
    int erW_ = 0;
    double erGain_[12], erTL_[12], erTR_[12];
    // Входная диффузия: 8 all-pass (L 0–3, R 4–7) в одном буфере.
    std::vector<float> apBuf_;
    int apOff_[8], apMask_[8], apLen_[8], apW_[8];
    double apG_[4];
    // FDN: 8 линий подряд.
    int lineSize_, mask_;
    std::vector<float> lines_;
    int w_ = 0;
    double base_[8], gain_[8], damp_[8], lp_[8], rd_[8], v_[8];
    // LFO линий: вращающийся вектор (sin, cos) и шаг поворота.
    double ms_[8], mc_[8], mr_[8], mi_[8];
    double modDepth_, ret_;
};

}  // namespace jambo
