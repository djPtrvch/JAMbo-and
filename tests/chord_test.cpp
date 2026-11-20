// Режим CHORD (порт ChordTests.swift без случайных прогрессий): названия, римские ступени, ступени и места аккордов.
#include "model/Chords.h"
#include "model/Scales.h"
#include <cstdio>
#include <string>

using namespace jambo;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}
constexpr int phr = 3, e = 4;

void phrygianTriads() {
    const char* want[7] = {"Em", "F", "G", "Am", "B°", "C", "Dm"};
    for (int d = 0; d < 7; ++d)
        check(Chords::name(Scales::chord(phr, e, 2, d, 0), 0) == want[d], std::string("phrygian ") + want[d]);
    check(Chords::keyLabel(phr, e, 2, 0, 0) == "Em · i", "keyLabel Em · i");
    check(Chords::keyLabel(phr, e, 2, 1, 0) == "F · II", "keyLabel F · II");
    check(Chords::keyLabel(phr, e, 2, 4, 0) == "B° · v°", "keyLabel B° · v°");
    check(Chords::keyLabel(phr, e, 2, 7, 0) == "Em · i", "keyLabel через октаву");
}

void allNotesInScale() {
    bool ok = true;
    for (int scale = 0; scale < int(Scales::all.size()); ++scale) {
        const auto& steps = Scales::all[size_t(scale)].steps;
        for (int root = 0; root < 12; ++root)
            for (int kind = 0; kind < int(Scales::chordKinds.size()); ++kind)
                for (int d = 0; d < Scales::keyCount(scale, 2); ++d) {
                    const auto c = Scales::chord(scale, root, 2, d, kind);
                    if (c.size() != Scales::chordDegrees[size_t(kind)].size()) ok = false;
                    for (size_t i = 1; i < c.size(); ++i)
                        if (c[i] < c[i - 1]) ok = false;
                    for (int n : c) {
                        bool in = false;
                        for (int s : steps) in = in || s == Scales::wrap(n - root, 12);
                        ok = ok && in;
                    }
                    if (Chords::name(c, kind).empty()) ok = false;
                }
    }
    check(ok, "все ноты аккордов — в гамме, имена есть");
}

void formulas() {
    auto name = [](int d, int k) { return Chords::name(Scales::chord(0, 0, 2, d, k), k); };
    check(name(0, 1) == "Cmaj7", "Cmaj7");
    check(name(0, 2) == "Cmaj9", "Cmaj9");
    check(name(0, 3) == "Csus2", "Csus2");
    check(name(0, 4) == "Csus4", "Csus4");
    check(name(0, 5) == "Cadd9", "Cadd9");
    check(name(1, 5) == "Dm(add9)", "Dm(add9)");
    check(name(4, 1) == "G7", "G7");
    check(name(4, 2) == "G9", "G9");
    check(name(1, 1) == "Dm7", "Dm7");
    check(name(6, 1) == "Bm7b5", "Bm7b5");
    check(name(3, 4) == "F(sus4)", "F(sus4)");
    check(Scales::chord(0, 0, 2, 4, 2) == std::vector<int>{43, 47, 50, 53, 57}, "G9 ноты");
}

void progressionPlaces() {
    const auto cs = Chords::chords({1, 7, 6, 7}, phr, e, 2, 0);
    std::string names;
    for (const auto& c : cs) names += Chords::name(c, 0) + " ";
    check(names == "Em Dm C Dm ", "прогрессия по гамме");
    check(Chords::degreeIndex(6, 8) == 0 && Chords::degreeIndex(7, 8) == 1, "пентатоника по модулю");
    const auto p1 = Chords::places(1), p4 = Chords::places(4), p2 = Chords::places(2);
    check(p1[1].first == 4 && p1[3].first == 12 && p1[0].second == 4, "places 1");
    check(p4[1].first == 16 && p4[3].first == 48 && p4[0].second == 16, "places 4");
    check(p2[1].first == 8 && p2[3].first == 24, "places 2");
}
}  // namespace

int runChordTests() {
    phrygianTriads();
    allNotesInScale();
    formulas();
    progressionPlaces();
    std::printf("chords: %d / %d passed\n", checks - fails, checks);
    return fails;
}
