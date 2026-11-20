// Измеритель справа и метры MIX вне колбэка (порт MeterState.swift, ветка swift JAMbo): 60 раз в секунду — новые блоки
// из Levels, спад полосы ×0,9 за кадр, пик-холд 1,5 с (затем ×0,95 за кадр), метка клиппинга 2 с, наибольший пик
// с последнего сброса.
#pragma once
#include "engine/Levels.h"
#include <vector>

namespace jambo {

struct MeterBar {
    float level = 0, hold = 0;
};

class MeterState {
public:
    static constexpr int channels = Levels::channels;
    /// Пик ≥ этого — перегруз.
    static constexpr float over = 0.999f;

    std::vector<float> disp = std::vector<float>(size_t(channels * 2), 0.f);
    std::vector<float> hold = std::vector<float>(size_t(channels * 2), 0.f);
    std::vector<float> duck = std::vector<float>(size_t(channels), 0.f);
    std::vector<float> maxPeak = std::vector<float>(size_t(channels), 0.f);
    /// Палец на регуляторе громкости измерителя: снизу вместо пика — его значение.
    bool volDrag = false;

    void tick(const Levels& lv, double now);
    /// Канал измерителя сменился: пик и LUFS — заново.
    void show(int ch, Levels& lv);
    float framePeak(int ch) const;
    void resetPeak(int ch);
    bool clip(int ch, double now) const;
    /// Пики и холды канала: стерео — L и R, моно — наибольший.
    std::vector<MeterBar> bars(int ch, bool stereo) const;
    /// Снижение громкости от Sidechain, dB (≤ 0).
    float duckDb(int ch) const;
    int shown() const { return shown_; }

private:
    std::vector<double> holdAt_ = std::vector<double>(size_t(channels * 2), 0.0);
    std::vector<double> clipUntil_ = std::vector<double>(size_t(channels), 0.0);
    std::vector<float> peak_ = std::vector<float>(size_t(channels * 2), 0.f);
    std::vector<float> newDuck_ = std::vector<float>(size_t(channels), 0.f);
    long count_ = 0;
    int shown_ = -1;
};

}  // namespace jambo
