// Аранжировщик: данные таймлайна (порт структур Core/Model/Arrange.swift, ветка swift JAMbo; правки — позже).
// Позиции и длины — в шагах транспорта (16 на такт) от начала аранжировки.
#pragma once
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "ArrangeAuto.h"
#include "Clip.h"

namespace jambo {

namespace ArrPalette {
constexpr int count = 20;
constexpr int defaultColor = 17;
constexpr int white = 16;
constexpr int removed = 3;
}  // namespace ArrPalette

namespace ArrGrid {
constexpr int stepsPerBar = 16;
constexpr int steps[4] = {16, 8, 4, 1};
}  // namespace ArrGrid

/// Неизменённые копии клипа разделяют память; перед правкой блок получает свою копию.
class ArrPattern {
public:
    ArrPattern() = default;
    ArrPattern(Clip value) : clip_(std::make_shared<Clip>(std::move(value))) {}
    ArrPattern(std::optional<Clip> value) {
        if (value) clip_ = std::make_shared<Clip>(std::move(*value));
    }
    bool has_value() const { return bool(clip_); }
    explicit operator bool() const { return has_value(); }
    const Clip& operator*() const { return *clip_; }
    const Clip* operator->() const { return clip_.get(); }
    Clip& edit() {
        if (!clip_.unique()) clip_ = std::make_shared<Clip>(*clip_);
        return *clip_;
    }
    bool operator==(const ArrPattern& other) const {
        return clip_ == other.clip_ || (clip_ && other.clip_ && *clip_ == *other.clip_);
    }
    bool operator==(const Clip& value) const { return clip_ && *clip_ == value; }

private:
    std::shared_ptr<Clip> clip_;
};

/// Блок на дорожке: клип сетки, начало и длина в шагах, offset — с какого шага прохода клипа; pattern — своя копия
/// клипа (nullopt — играет клип сетки); autos — автоматизация блока; legacyLink — LINK прежних проектов (не пишется).
struct ArrBlock {
    int clip = 0;
    int start = 0;
    int length = ArrGrid::stepsPerBar;
    int offset = 0;
    ArrPattern pattern;
    std::vector<ArrAutoLane> autos;
    bool legacyLink = false;

    ArrBlock() = default;
    ArrBlock(int c, int s, int l, int o = 0, ArrPattern p = {})
        : clip(c), start(s), length(l), offset(o), pattern(std::move(p)) {}
    int end() const { return start + length; }
    /// Начало блока сдвинулось на d шагов вправо: точки — на прежних местах таймлайна.
    void shiftAutos(int d);
    /// Точка на шаге x блока у кривой param, если там точки ещё нет (строки нет — появляется).
    void pinAuto(const std::string& param, double x, double y);
    /// Разрез на шаге x блока: у кривых, которые через x проходят, — точка на x.
    void pinCut(int x);
    auto tie() const { return std::tie(clip, start, length, offset, pattern, autos, legacyLink); }
    JAMBO_EQ(ArrBlock)
};

/// Маркер секции: позиция, имя, цвет, конец (nullopt — до следующего маркера).
struct ArrMarker {
    int at = 0;
    std::string name;
    int color = ArrPalette::defaultColor;
    std::optional<int> end;
    auto tie() const { return std::tie(at, name, color, end); }
    JAMBO_EQ(ArrMarker)
};

/// Референс-трек: ключ сэмпла, имя, сдвиг (с), громкость (dB), темп файла.
struct ArrRef {
    std::string key;
    std::string name;
    double offset = 0;
    double gain = 0;
    std::optional<double> bpm;
    auto tie() const { return std::tie(key, name, offset, gain, bpm); }
    JAMBO_EQ(ArrRef)
};

/// Общие данные аранжировки (кроме блоков).
struct Arrangement {
    std::vector<ArrMarker> markers;
    int loopFrom = 0;
    int loopTo = 0;
    bool loop = false;
    int grid = 2;
    bool snap = true;
    bool hideSteps = false;
    std::optional<ArrRef> ref;
    bool showRef = false;

    /// Шаг сетки в шагах транспорта.
    int gridSteps() const { return ArrGrid::steps[std::max(0, std::min(grid, 3))]; }
    /// Позиция с учётом магнита.
    int snapped(int x) const {
        if (!snap) return std::max(0, x);
        const int g = gridSteps();
        return (std::max(0, x) + g / 2) / g * g;
    }
    bool hasLoop() const { return loopTo > loopFrom; }
    /// После чтения: маркеры по порядку, в пределах; петля и сетка — в пределах.
    void repair();
    auto tie() const { return std::tie(markers, loopFrom, loopTo, loop, grid, snap, hideSteps, ref, showRef); }
    JAMBO_EQ(Arrangement)
};

}  // namespace jambo
