// Шаг и клип (порт Step и Clip, Core/Model/Project.swift, ветка swift JAMbo).
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Params.h"
#include "TrackSound.h"

namespace jambo {

struct Step {
    bool on = false;
    /// Длина ноты в шагах.
    int len = 1;
    /// Ноты шага (MIDI). Несколько нот — POLY.
    std::vector<int> notes;
    /// Длины нот (параллельно notes); nullopt — у всех len.
    std::optional<std::vector<int>> lens;
    /// Velocity нот 0…127 (−1 — без своей); nullopt — ни у одной своей нет.
    std::optional<std::vector<double>> vels;
    /// P-lock: id параметра → значение.
    ParamMap locks;
    /// Свой звук шага.
    std::optional<std::string> sound;
    /// P-lock, которые поставил пресет звука шага.
    std::optional<std::vector<std::string>> soundLocks;

    /// Длины и velocity нот — параллельно notes (иначе снимаются); при lens len — самая длинная.
    void repair();

    // Ноты шага (StepNotes.swift): StepNotes.cpp
    /// Длина ноты k в шагах.
    int noteLen(int k) const;
    /// Своя velocity ноты k (Level 0…127); nullopt — Level шага или дорожки.
    std::optional<double> noteVel(int k) const;
    /// Хоть у одной ноты своя velocity.
    bool hasVel() const { return vels.has_value(); }
    /// Длина всех нот шага — поштучные длины забываются.
    void setAllLen(int n);
    /// Новый набор нот: у оставшихся — прежние длины и velocity, у новых — len шага, без своей velocity.
    void setNotes(const std::vector<int>& next);
    /// Добавить ноту (или сменить длину и velocity, если такая уже есть).
    void addNote(int n, int l, std::optional<double> vel);
    /// Убрать ноту k; шаг без нот — без трига.
    void removeNote(int k);
    void setNoteLen(int k, int n);
    /// nullopt — снять свою velocity.
    void setNoteVel(int k, std::optional<double> v);
    /// Velocity ноты k, связанная с p-lock Level шага.
    void setVelLinked(int k, double v, double fallback);
    void clearVels() { vels.reset(); }
    /// Одинаковые длины — одна len; без своих velocity — vels nullopt; len — самая длинная нота.
    void normalize();

    auto tie() const { return std::tie(on, len, notes, lens, vels, locks, sound, soundLocks); }
    JAMBO_EQ(Step)
};

using Page = std::vector<Step>;

/// Пустая страница (16 шагов).
inline Page emptyPage() { return Page(size_t(Params::stepsPerPage)); }

/// Играющие страницы: from…to включительно.
struct PageRange {
    int lower, upper;
};

/// Клип — ячейка сетки паттернов со своим паттерном: страницы (до 8), длина, играющие страницы, гамма, нота, FX.
struct Clip {
    std::vector<Page> pages = {emptyPage()};
    int length = Params::stepsPerPage;
    int playFrom = 0;
    int playTo = 0;
    std::optional<int> scale;
    std::optional<int> root;
    std::optional<ParamMap> fx;
    /// Переход MIX: только у клипа строки PREV — звук старой дорожки.
    std::optional<TrackSound> src;
    /// Скорость паттерна: степень двойки — 1 (×2), 0 или nullopt (×1), −1…−4.
    std::optional<int> rate;

    static const std::vector<int>& rates();
    static const std::vector<std::string>& rateNames();

    int speed() const { return rate.value_or(0); }
    /// Проход в тиках транспорта с учётом скорости.
    int passTicks() const;
    /// Позиция прохода, которая звучит на тике o от начала клипа.
    int localStep(int o) const;
    /// Играющие страницы в пределах страниц клипа.
    PageRange range() const;
    /// Шагов на странице p у прохода, который начинается со страницы from.
    int pageLen(int p, int from) const;
    /// Шагов в проходе.
    int playLen() const;
    int playLen(int from, int to) const;
    /// Шаг (p·16 + s) на позиции n прохода, по кругу.
    int playIndex(int n) const;
    /// Шаг по индексу; nullptr — вне страниц.
    const Step* step(int index) const;
    /// Шаг за длиной паттерна — не играет.
    bool outside(int index) const;
    /// После чтения: страницы, длина, играющие страницы, гамма, скорость — в пределах.
    void repair();

    auto tie() const { return std::tie(pages, length, playFrom, playTo, scale, root, fx, src, rate); }
    JAMBO_EQ(Clip)
};

/// Строка сетки (сцена): имя (пустое — номер) и число повторов.
struct SceneRow {
    std::string name;
    int repeats = 1;
    auto tie() const { return std::tie(name, repeats); }
    JAMBO_EQ(SceneRow)
};

}  // namespace jambo
