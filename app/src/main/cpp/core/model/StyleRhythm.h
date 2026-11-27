// Внутренние таблицы, отрезки гармонии и ритмические ячейки StyleParts.swift.
#pragma once
#include "MelodyRhythm.h"

namespace jambo::MelodyRhythm::Detail {
struct StyleNote {
    int step;
    Ints degrees;
    int length;
    double velocity;
};
using StyleNotes = std::vector<StyleNote>;
using Voices = std::vector<Ints>;

struct StyleTable {
    std::vector<Ints> rows, lens;
    std::vector<double> weights;
    explicit StyleTable(std::string_view text);
    size_t pick(Rng& rng, int exclude = -1) const;
};
struct StyleContext {
    int style, scaleSize;
    const Ints& lengths;
    Ints playing, starts, spans, roots;
    Kicks kicks;
    int count() const { return int(playing.size()); }
    int length(int page) const { return lengths[size_t(playing[size_t(page)])]; }
    size_t segment(int step) const;
    int root(int step) const { return roots[segment(step)]; }
};

extern const char* const bassCells[5];
extern const char* const bassPitch[5];
extern const char* const leadCells[5];
extern const char* const keysCells[5];
extern const char* const arpCells[5];
extern const char* const arpFigures[5];

double styleVelocity(Role role, int style, int step, bool first, Rng& rng);
Ints styleCells(const Ints& cell, const StyleContext& c, int vary, int cut, bool avoid, Rng& rng);
void fit(StyleNotes& notes);
StyleNotes hold(const StyleContext& c, const Voices& voices, Role role, Rng& rng);
StyleNotes styleRole(const Part& part, const StyleContext& c, Rng& rng);
}
