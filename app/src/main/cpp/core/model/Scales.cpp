// Порт Core/Model/Scales.swift (ветка swift JAMbo).
#include "Scales.h"
#include <algorithm>
#include <cmath>

namespace jambo {
namespace Scales {

const std::vector<Scale> all = {
    {"Major", "Maj", {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", "Min", {0, 2, 3, 5, 7, 8, 10}},
    {"Dorian", "Dor", {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian", "Phr", {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian", "Lyd", {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian", "Mix", {0, 2, 4, 5, 7, 9, 10}},
    {"Locrian", "Loc", {0, 1, 3, 5, 6, 8, 10}},
    {"Harm Minor", "HMin", {0, 2, 3, 5, 7, 8, 11}},
    {"Pent Major", "PMaj", {0, 2, 4, 7, 9}},
    {"Pent Minor", "PMin", {0, 3, 5, 7, 10}},
    {"Chromatic", "Chr", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};

const std::vector<std::string> chordKinds = {"Triad", "7th", "9th", "Sus2", "Sus4", "Add9"};
const std::vector<std::vector<int>> chordDegrees = {
    {0, 2, 4}, {0, 2, 4, 6}, {0, 2, 4, 6, 8}, {0, 1, 4}, {0, 3, 4}, {0, 2, 4, 8}};

int wrap(int v, int n) { return ((v % n) + n) % n; }

int baseNote(int root, int octave) { return 12 * (octave + 1) + root; }

int note(int scale, int root, int octave, int k) {
    const std::vector<int>& steps = all[size_t(wrap(scale, int(all.size())))].steps;
    const int n = int(steps.size());
    const int oct = int(std::floor(double(k) / double(n)));
    return baseNote(root, octave) + 12 * oct + steps[size_t(wrap(k, n))];
}

int keyCount(int scale, int octaves) {
    return std::min(maxKeys, int(all[size_t(wrap(scale, int(all.size())))].steps.size()) * octaves + 1);
}

int topOctave(int scale, int root, int octaves, bool chordMode, int kind) {
    const int last = keyCount(scale, octaves) - 1;
    int o = octaveMax;
    while (o > octaveMin) {
        int top = 0;
        if (chordMode) {
            const std::vector<int> c = chordRaw(scale, root, o, last, kind);
            top = c.empty() ? 0 : *std::max_element(c.begin(), c.end());
        } else {
            top = note(scale, root, o, last);
        }
        if (top <= 127) break;
        --o;
    }
    return o;
}

std::vector<int> chordRaw(int scale, int root, int octave, int degree, int kind) {
    const int n = int(chordDegrees.size());
    const std::vector<int>& ds = chordDegrees[size_t(std::min(n - 1, std::max(0, kind)))];
    std::vector<int> out;
    out.reserve(ds.size());
    for (int d : ds) out.push_back(note(scale, root, octave, degree + d));
    return out;
}

std::vector<int> chord(int scale, int root, int octave, int degree, int kind) {
    std::vector<int> c = chordRaw(scale, root, octave, degree, kind);
    for (int& m : c) while (m > 127) m -= 12;
    return c;
}

}  // namespace Scales
}  // namespace jambo
