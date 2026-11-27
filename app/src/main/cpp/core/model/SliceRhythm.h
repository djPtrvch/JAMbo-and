// Куски нарезки и генерация шагов с нотами либо p-lock START/END.
#pragma once
#include "Clip.h"
#include "MelodyRhythm.h"
#include "Sounds.h"

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

std::vector<SlicePiece> slicePieces(const Track& track, SampleData& sample, const Project& project,
                                    SliceMap map = SliceMap::chromatic, bool follow = false);
RhythmPattern pattern(const std::vector<SlicePiece>& pieces, const RhythmRandom::Ints& lengths, int style,
                      const RhythmRandom::Ints* last, RhythmRandom::Rng& rng);
RhythmRandom::Ints code(const std::vector<RhythmNote>& events);
void writePage(Page& page, const std::vector<SlicePiece>& pieces, const std::vector<RhythmNote>& events,
               const Track& track, int note, bool arpSteps = false);
}  // namespace SliceRhythm
}  // namespace jambo
