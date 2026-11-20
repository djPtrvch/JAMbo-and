// FREQ, MASK и LOUD при MIX (порт PowerFFT и MixAnalysis из MixAnalysis.swift, ветка swift JAMbo): по кадру — две
// дорожки по кругу; мощность в 30 полосах (по 1/3 октавы, 20 Гц…20 кГц), среднее за ~4 с; диапазон дорожки — полосы
// не ниже наибольшей на 20 dB, пик — наибольшая. LOUD — средний квадрат с весами кривой K (BS.1770). MASK — полосы
// диапазона, где основное звучание других слышимых не тише дорожки больше чем на 12 dB (сглажено).
#pragma once
#include <optional>
#include <vector>
#include "engine/TrackTaps.h"
#include "eq/Spectrum.h"

namespace jambo {

/// Спектр мощности по 4096 отсчётам (окно Ханна) — сложить в полосы.
class PowerFFT {
public:
    static constexpr int size = 4096;
    static constexpr int bins = size / 2;
    static constexpr float hz = 48000.f / 4096.f;
    PowerFFT();
    void bands(const float* x, const std::vector<int>& band, std::vector<float>& out);
    static std::vector<int> bandMap(const std::vector<float>& edges);

private:
    std::vector<float> window, input, re, im;
    RealFFT fft{size};
};

class MixAnalysis {
public:
    static constexpr int bands = 30;
    static constexpr float range = 20;
    static constexpr float maskRange = 12, maskSig = 10, maskHyst = 3, maskShow = 0.7f;
    static constexpr int tracks = TrackTaps::tracks;

    MixAnalysis();
    void reset();
    void tick(const TrackTaps& taps, int count, const std::vector<bool>& audible, bool mask);

    struct Span {
        float lo, hi, peak;
    };
    std::optional<Span> span(int t) const;
    float loud(int t) const;
    struct Mask {
        std::vector<float> strength;
        std::vector<bool> buried;
        int who;
    };
    Mask mask(int t) const;
    static double kPower(double f);

private:
    PowerFFT fftp;
    std::vector<int> map;
    std::vector<float> raw = std::vector<float>(PowerFFT::size, 0.f);
    std::vector<std::vector<float>> power;
    std::vector<float> fresh = std::vector<float>(bands, 0.f);
    std::vector<float> rms = std::vector<float>(tracks, 0.f), kms = std::vector<float>(tracks, 0.f);
    std::vector<float> kWeight;
    int next = 0;
    std::vector<std::vector<float>> mStr;
    std::vector<std::vector<bool>> mBur;
    std::vector<int> mWho = std::vector<int>(tracks, -1), whoCand = std::vector<int>(tracks, -1),
                     whoHold = std::vector<int>(tracks, 0);
    void analyze(const TrackTaps& taps, int t, float a);
    void updateMask(int count, const std::vector<bool>& audible);
};

}  // namespace jambo
