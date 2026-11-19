// Телеметрия модулей мастера для индикации COMP / CLIP / LIM (порт Core/Master/DynTele.swift, ветка swift JAMbo): кадр раз
// в 256 отсчётов — пик входа и выхода (линейно), GR (dB ≥ 0), уровень детектора (dB, у компрессора). Колбэк пишет кадры
// в кольцо, интерфейс читает новые.
#pragma once
#include <algorithm>
#include <vector>
#include "util/AtomicFloat.h"

namespace jambo {

class DynTele {
public:
    static constexpr int capacity = 2048;
    static constexpr int mask = capacity - 1;
    DynTele() : inPeak(capacity, 0.f), gr(capacity, 0.f), outPeak(capacity, 0.f), det(capacity, 0.f) {}
    std::vector<float> inPeak, gr, outPeak, det;
    AtomicCounter written;

    /// Колбэк: новый кадр модуля (frames — его счётчик кадров; тот же — ничего не пишет).
    void push(long frames, double i, double g, double o, double d) {
        if (frames == seen) return;
        seen = frames;
        const long w = written.load();
        const size_t k = size_t(w & mask);
        inPeak[k] = float(i);
        gr[k] = float(g);
        outPeak[k] = float(o);
        det[k] = float(d);
        written.store(w + 1);
    }

    struct Peaks {
        long count;
        float inPeak, gr, outPeak;
        long frames;
    };
    /// Интерфейс: наибольшие вход, GR и выход в кадрах после since; новый счётчик.
    Peaks peaks(long since) const {
        const long w = written.load();
        long k = std::max(since, w - capacity + 1);
        float i = 0, g = 0, o = 0;
        const long got = w - k;
        for (; k < w; ++k) {
            const size_t q = size_t(k & mask);
            i = std::max(i, inPeak[q]);
            g = std::max(g, gr[q]);
            o = std::max(o, outPeak[q]);
        }
        return {w, i, g, o, std::max(0L, got)};
    }

private:
    long seen = 0;
};

}  // namespace jambo
