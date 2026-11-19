// Sidechain: огибающая приглушения e (0…1; 1 — полное приглушение), один экземпляр на проект, входа нет.
// Порт 1:1 Core/FX/Duck.swift (ветка swift JAMbo); поведение — docs/ai/fx-eq.md JAMbo.
// Совпадает с эталонами CoreTests/golden/fx_duck_*.i16 по отсчётам. Громкость дорожки = 1 − amount·e.
// Очередь тригов — фиксированный массив (без выделений в колбэке).
#pragma once
#include <cstdint>

namespace jambo {

class FxDuck {
public:
    explicit FxDuck(double sampleRate) : sr_(sampleRate) {}

    /// Триг источника в кадре frame (вставка по кадру; переполнение — самый ранний выбрасывается).
    void trig(int64_t frame, double attack, double release, double curve);
    /// Остановка: будущие триги отменяются.
    void flush() { count_ = 0; }
    /// Блок n отсчётов с кадра f0: e в out (перезаписывает).
    void process(int64_t f0, float* out, int n);

    double e() const { return e_; }
    /// Индикация: раз в 512 отсчётов, пока e > 0.001, и раз при возврате к 0.
    double meter() const { return meter_; }

    static constexpr int maxQueue = 256;

private:
    void removeFirst();
    void start(double attack, double release, double curve);
    void report(int n);

    double sr_;
    double e_ = 0;
    int stage_ = 0;
    double pos_ = 0, e0_ = 0, A_ = 1, R_ = 1, q_ = 2;
    int64_t qFrame_[maxQueue] = {};
    double qAttack_[maxQueue] = {}, qRelease_[maxQueue] = {}, qCurve_[maxQueue] = {};
    int count_ = 0;
    int sinceReport_ = 0;
    double reported_ = 0;
    double meter_ = 0;
};

}  // namespace jambo
