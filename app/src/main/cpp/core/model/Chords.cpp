// Порт Core/Model/Chords.swift (ветка swift JAMbo): названия, ступени, места аккордов прогрессии.
#include "Chords.h"
#include "Params.h"
#include "Scales.h"
#include <algorithm>

namespace jambo {
namespace Chords {

namespace {
const char* const romans[12] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
const char* const noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
}  // namespace

std::vector<std::pair<int, int>> places(int pages) {
    const int len = std::max(1, pages) * Params::stepsPerPage / count;
    std::vector<std::pair<int, int>> out;
    for (int i = 0; i < count; ++i) out.push_back({i * len, len});
    return out;
}

int degreeIndex(int degree, int scale) {
    const int n = int(Scales::all[size_t(Scales::wrap(scale, int(Scales::all.size())))].steps.size());
    return Scales::wrap(degree - 1, n);
}

std::string name(const std::vector<int>& notes, int kind) {
    if (notes.empty()) return "";
    const int root = notes.front();
    const std::string base = noteNames[Scales::wrap(root, 12)];
    std::vector<int> iv;
    for (int n : notes) iv.push_back(Scales::wrap(n - root, 12));
    if (auto q = quality(iv, kind)) return base + *q;
    const int k = std::min(int(Scales::chordKinds.size()) - 1, std::max(0, kind));
    return base + "(" + lower(Scales::chordKinds[size_t(k)]) + ")";
}

std::optional<std::string> quality(const std::vector<int>& iv, int kind) {
    auto at = [&](size_t i) { return i < iv.size() ? iv[i] : -1; };
    const auto triad = triadName(at(1), at(2));
    switch (kind) {
    case 0: return triad;
    case 1: return seventhName(at(1), at(2), at(3));
    case 2: {
        const auto s = seventhName(at(1), at(2), at(3));
        if (!s) return std::nullopt;
        return ninthName(*s, at(4));
    }
    case 3: return at(1) == 2 && at(2) == 7 ? std::optional<std::string>("sus2") : std::nullopt;
    case 4: return at(1) == 5 && at(2) == 7 ? std::optional<std::string>("sus4") : std::nullopt;
    case 5: {
        if (!triad || at(3) != 2) return std::nullopt;
        if (*triad == "") return std::string("add9");
        if (*triad == "m") return std::string("m(add9)");
        return *triad + "(add9)";
    }
    default: return std::nullopt;
    }
}

std::optional<std::string> triadName(int third, int fifth) {
    if (third == 4 && fifth == 7) return std::string("");
    if (third == 3 && fifth == 7) return std::string("m");
    if (third == 3 && fifth == 6) return std::string("°");
    if (third == 4 && fifth == 8) return std::string("+");
    return std::nullopt;
}

std::optional<std::string> seventhName(int third, int fifth, int seventh) {
    struct Row { int t, f, s; const char* n; };
    static const Row rows[] = {{4, 7, 11, "maj7"}, {4, 7, 10, "7"},    {3, 7, 10, "m7"},    {3, 6, 10, "m7b5"},
                               {3, 6, 9, "°7"},    {3, 7, 11, "mMaj7"}, {4, 8, 11, "+maj7"}, {4, 8, 10, "+7"}};
    for (const Row& r : rows)
        if (r.t == third && r.f == fifth && r.s == seventh) return std::string(r.n);
    return std::nullopt;
}

std::optional<std::string> ninthName(const std::string& seventh, int ninth) {
    if (ninth == 2) {
        if (seventh == "maj7") return std::string("maj9");
        if (seventh == "7") return std::string("9");
        if (seventh == "m7") return std::string("m9");
        if (seventh == "mMaj7") return std::string("mMaj9");
        return seventh + "(9)";
    }
    if (ninth == 1) return seventh + "(b9)";
    if (ninth == 3) return seventh + "(#9)";
    return std::nullopt;
}

std::string numeral(int degree, const std::vector<int>& notes) {
    const std::string r = romans[Scales::wrap(degree, 12)];
    if (notes.size() <= 2) return r;
    const int root = notes[0];
    const int third = Scales::wrap(notes[1] - root, 12);
    const int fifth = Scales::wrap(notes[2] - root, 12);
    if (third == 3) return lower(r) + (fifth == 6 ? "°" : "");
    return r + (third == 4 && fifth == 8 ? "+" : "");
}

std::string keyLabel(int scale, int root, int octave, int degree, int kind) {
    const std::vector<int> notes = Scales::chord(scale, root, octave, degree, kind);
    const int n = int(Scales::all[size_t(Scales::wrap(scale, int(Scales::all.size())))].steps.size());
    return name(notes, kind) + " · " + numeral(Scales::wrap(degree, n), notes);
}

std::vector<std::vector<int>> chords(const std::vector<int>& degrees, int scale, int root, int octave, int kind) {
    std::vector<std::vector<int>> out;
    for (int d : degrees) out.push_back(Scales::chord(scale, root, octave, degreeIndex(d, scale), kind));
    return out;
}

}  // namespace Chords
}  // namespace jambo
