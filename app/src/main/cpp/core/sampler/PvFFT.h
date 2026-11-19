// БПФ вещественного сигнала для фазового вокодера (порт Core/Sampler/PvFFT.swift, ветка без Accelerate; ветка swift
// JAMbo): вперёд — n отсчётов → бины 0…n/2 (Re, Im; у 0 и n/2 Im = 0), назад — бины → n отсчётов (умноженных на gain).
// Комплексное БПФ n/2 точек (чётные отсчёты — Re, нечётные — Im) и разделение спектра. Создаётся вне колбэка; forward и
// inverse — без выделений памяти.
#pragma once
#include <vector>

namespace jambo {

class PvFFT {
public:
    explicit PvFFT(int size);
    const int n;
    const int half;
    int bits = 0;
    /// Круг forward → inverse: x → gain·x.
    float gain() const { return gain_; }
    /// x (n отсчётов) → re, im (n/2 + 1 бинов).
    void forward(const float* x, float* re, float* im);
    /// re, im (n/2 + 1 бинов) → x (n отсчётов, умноженных на gain).
    void inverse(const float* re, const float* im, float* x);

private:
    float gain_ = 1;
    std::vector<float> cr, ci, wc, ws, sc, ss;
    std::vector<int> rev;
    void complex(bool inv);
};

}  // namespace jambo
