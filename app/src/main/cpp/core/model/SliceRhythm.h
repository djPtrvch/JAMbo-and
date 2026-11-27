// Куски нарезки и генерация шагов с нотами либо p-lock START/END.
#pragma once
#include "Clip.h"
#include <random>

namespace jambo {
struct Track;
struct Project;
class SampleData;

namespace SliceRhythm {
struct SlicePiece {
    int note;
    int length;
    double start;
    double end;
    int role;
};

std::vector<SlicePiece> slicePieces(const Track& track, SampleData& sample, const Project& project);
void writePage(Page& page, const std::vector<SlicePiece>& pieces, const Track& track, int note,
               int length, int style, std::vector<int>* last, std::mt19937& rng);
}  // namespace SliceRhythm
}  // namespace jambo
