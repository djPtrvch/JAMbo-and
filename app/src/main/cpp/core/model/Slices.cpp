// Порт Slices (Core/Model/Sounds.swift, ветка swift JAMbo).
#include <set>
#include "Scales.h"
#include "Sounds.h"

namespace jambo {
namespace Slices {

int note(int idx, SliceMap map, int scale, int root, int octave) {
    if (map != SliceMap::scale) return Scales::baseNote(root, octave) + idx;
    return Scales::note(scale, root, octave, idx);
}

std::vector<int> notes(const std::vector<SliceMark>& m, SliceMap map, int scale, int root, int /*keys*/) {
    // Проба: октава — fixedOctave (3), иначе — октава клавиатуры.
    const int octave = fixedOctave;
    std::set<int> taken;
    for (const auto& v : m)
        if (!v.isFree() && v.note) taken.insert(*v.note);
    int j = 0;
    std::vector<int> out;
    for (const auto& v : m) {
        if (v.isFree()) {
            out.push_back(noNote);
            continue;
        }
        if (v.note) {
            j += 1;
            out.push_back(*v.note);
            continue;
        }
        while (taken.count(note(j, map, scale, root, octave))) j += 1;
        out.push_back(note(j, map, scale, root, octave));
        j += 1;
    }
    return out;
}

}  // namespace Slices
}  // namespace jambo
