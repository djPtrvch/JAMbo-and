// Источник такта (порт Core/Sequencer/TempoSource.swift, ветка swift JAMbo): темп и фаза, по которым секвенсор ставит
// шаги. Время — секунды часов звука (кадры ядра / 48000), доля — четверть, шаг — 1/4 доли. Link — позже.
#pragma once
#include <cmath>

namespace jambo {

namespace TempoGrid {
/// Долей в такте: граница такта — запуск паттернов и строк, акцент метронома.
constexpr double quantumBeats = 4.0;
/// Шагов (1/16) в доле.
constexpr double stepsPerBeat = 4.0;
inline bool isBarStart(double beat) {
    const double q = beat / quantumBeats;
    return std::fabs(q - std::round(q)) < 1e-9;
}
}  // namespace TempoGrid

class TempoSource {
public:
    virtual ~TempoSource() = default;
    virtual double tempo() const = 0;
    /// Доля (от начала игры) в момент time, с.
    virtual double beatAt(double time) const = 0;
    /// Момент, с, для доли beat.
    virtual double timeAt(double beat) const = 0;
};

/// Свой источник: темп и точка отсчёта — доля b0 в момент t0. Смена темпа не сдвигает долю. Живёт в аудиопотоке.
class InternalTempoSource : public TempoSource {
public:
    explicit InternalTempoSource(double bpm) : tempo_(bpm) {}
    double tempo() const override { return tempo_; }
    double beatAt(double time) const override { return b0 + (time - t0) * tempo_ / 60; }
    double timeAt(double beat) const override { return t0 + (beat - b0) * 60 / tempo_; }
    /// Старт: доля 0 — в момент at.
    void start(double at) {
        t0 = at;
        b0 = 0;
    }
    /// С доли beat действует новый темп; момент этой доли считается по прежнему.
    void retempo(double bpm, double beat) {
        if (bpm == tempo_) return;
        t0 = timeAt(beat);
        b0 = beat;
        tempo_ = bpm;
    }
    /// Сдвиг фазы: все доли на dt секунд позже (подстройка к MIDI-клоку).
    void shift(double dt) { t0 += dt; }
    /// Темп без игры — сразу.
    void set(double bpm) { tempo_ = bpm; }

private:
    double t0 = 0, b0 = 0, tempo_;
};

}  // namespace jambo
