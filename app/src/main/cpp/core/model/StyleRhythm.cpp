// Контекст гармонии, веса и ритм стилей House, Breaks, Trap, Pop, Ambient.
#include "StyleRhythm.h"
#include "Chords.h"
#include "Scales.h"
#include <cmath>
#include <cstdlib>

namespace jambo::MelodyRhythm::Detail {
using namespace RhythmRandom;

const char* const bassCells[5] = {
    "26ae:20 269ae:15 236abe:15 267aef:15 256ade:15 1269ae:10 26abef:10",
    "136be:20 13bf:20 157bd:15 367be:15 16bd:15 37bf:15",
    "",
    "02468ace:30 0368be:20 0268ae:15 048c:15 0258ad:20",
    "",
};
const char* const bassPitch[5] = {
    "0:60 1:20 2:10 3:10",
    "0:55 1:15 2:15 3:15",
    "0:75 1:8 2:8 3:9",
    "0:70 1:15 2:15",
    "0:1",
};
const char* const leadCells[5] = {
    "2369ae:1 1367ad:1 256abe:1 369be:1",
    "1367be:1 3569b:1 169bd:1 379ef:1",
    "0368b:1 026a:1 068e:1 038be:1",
    "0248c:1 02468a:1 0368c:1 2468c:1",
    "0:20 08:25 4c:20 06c:20 2a:15",
};
const char* const keysCells[5] = {
    "26ae:25 25ad:20 36be:20 269e:20 2a:15",
    "169e:25 36bd:25 159d:15 37bf:15 67b:20",
    "0:20 08:30 06a:25 038:25",
    "048c:30/22 02468ace:30/11 0368be:25/12 26ae:15/11",
    "0:30 08:25 06:20 0246:25",
};
const char* const arpCells[5] = {
    "",
    "0368be:40 1369be:30 0257ad:30",
    "",
    "",
    "048c:40 0369c:30 0268c:30",
};
const char* const arpFigures[5] = {
    "0:40 1:30 2:30",
    "1:40 3:30 0:30",
    "0:50 1:50",
    "0:50 2:50",
    "4:60 0:40",
};

StyleTable::StyleTable(std::string_view text) {
    for (auto item : split(text)) {
        const auto parts = split(item, '/');
        const auto fields = split(parts[0], ':');
        rows.push_back(digits(fields[0]));
        weights.push_back(fields.size() > 1 ? std::atof(std::string(fields[1]).c_str()) : 1);
        lens.push_back(parts.size() > 1 ? digits(parts[1]) : Ints{});
    }
}

size_t StyleTable::pick(Rng& rng, int exclude) const {
    auto w = weights;
    if (w.empty()) return 0;
    if (exclude >= 0 && exclude < int(w.size()) && w.size() > 1) w[size_t(exclude)] = 0;
    return weighted(rng, w);
}

size_t StyleContext::segment(int step) const {
    return size_t(std::max<std::ptrdiff_t>(0, std::upper_bound(starts.begin(), starts.end(), step) - starts.begin() - 1));
}

double styleVelocity(Role role, int style, int step, bool first, Rng& rng) {
    constexpr int rules[5][5][4] = {
        {{0,70,85,100}, {2,70,90,100}, {3,85,100,100}, {1,70,85,100}, {5,70,85,100}},
        {{2,70,85,100}, {2,70,90,100}, {2,70,90,100}, {1,70,85,100}, {5,50,75,100}},
        {{4,70,85,100}, {2,70,85,100}, {3,70,85,100}, {1,70,85,100}, {5,55,75,100}},
        {{5,65,80,100}, {5,60,75,100}, {5,55,70,100}, {5,60,75,100}, {5,55,70,100}},
        {{0,60,80,100}, {2,60,80,100}, {6,55,75,100}, {1,60,80,100}, {2,40,65,70}},
    };
    const auto& r = rules[int(role)][style];
    bool strong = false;
    switch (r[0]) {
    case 0: strong = step % 4 == 2; break;
    case 1: strong = step % 4 == 0; break;
    case 2: strong = first; break;
    case 3: case 6: strong = step == 0; break;
    case 4: strong = step == 2 || step == 10; break;
    default: break;
    }
    if (r[0] == 6 && step == 8) return 114;
    return std::round((strong ? r[3] : integer(rng, r[1], r[2])) * 1.27);
}

Ints styleCells(const Ints& cell, const StyleContext& c, int vary, int cut, bool avoid, Rng& rng) {
    Ints out;
    for (int pi = 0; pi < c.count(); ++pi) {
        const int len = c.length(pi);
        const auto isKick = [&](int s) { return avoid && c.kicks[size_t(pi)].count(s) != 0; };
        Ints steps;
        for (int s : cell) if (s < len && !isKick(s)) steps.push_back(s);
        if (pi > 0 && pi < c.count() - 1 && vary > 0 && !steps.empty()) {
            Ints free;
            for (int s = 0; s < len; ++s)
                if (!isKick(s) && std::find(steps.begin(), steps.end(), s) == steps.end()) free.push_back(s);
            if (vary == 1) {
                Ints removable, addable;
                for (int s : steps) if (s % 4 != 2) removable.push_back(s);
                for (int s : free) if (s % 4 != 2) addable.push_back(s);
                if (!removable.empty()) {
                    const int s = RhythmRandom::pick(rng, removable);
                    if (addable.empty() || coin(rng)) {
                        steps.erase(std::remove(steps.begin(), steps.end(), s), steps.end());
                        addable.clear();
                    }
                }
                if (!addable.empty()) steps.push_back(RhythmRandom::pick(rng, addable));
            } else if (vary == 4) {
                if (steps.size() > 1) steps.erase(steps.begin() + integer(rng, 1, int(steps.size()) - 1));
            } else if (steps.size() > 1) {
                const int i = vary == 3 ? 1 : integer(rng, 1, int(steps.size()) - 1);
                const int to = steps[size_t(i)] + (coin(rng) ? 1 : -1);
                const bool available = std::find(free.begin(), free.end(), to) != free.end();
                if (vary == 3) {
                    if (available) steps[size_t(i)] = to;
                } else if (available && coin(rng)) steps[size_t(i)] = to;
                else steps.erase(steps.begin() + i);
            }
        }
        std::sort(steps.begin(), steps.end());
        for (int s : steps) if (pi != c.count() - 1 || c.count() == 1 || s < cut) out.push_back(pi * per + s);
    }
    return out;
}

void fit(StyleNotes& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.step < b.step; });
    for (size_t i = 0; i < notes.size(); ++i) {
        auto& n = notes[i];
        const int next = i + 1 < notes.size() ? notes[i + 1].step - n.step : per;
        n.length = std::max(1, std::min({n.length, next, per - n.step % per}));
    }
}

StyleNotes hold(const StyleContext& c, const Voices& voices, Role role, Rng& rng) {
    StyleNotes out;
    for (size_t i = 0; i < c.roots.size();) {
        size_t j = i;
        while (j + 1 < c.roots.size() && c.roots[j + 1] == c.roots[i]) ++j;
        const int end = c.starts[j] + c.spans[j];
        for (int s = c.starts[i]; s < end; s += 32)
            out.push_back({s, voices[i], std::min(32, end - s), styleVelocity(role, c.style, s % per, false, rng)});
        i = j + 1;
    }
    return out;
}

RhythmPattern styled(const Part& part, const Ints& lengths, const Ints& degrees, const Kicks& kicks, Rng& rng) {
    RhythmPattern out(lengths.size());
    const auto playing = playingPages(lengths);
    if (playing.empty()) return out;
    const int ps = part.style == 0 ? 0 : part.style - 1;
    StyleContext c{ps, int(Scales::all[size_t(part.scale)].steps.size()), lengths, playing, {}, {}, {}, {}};
    const Ints d = degrees.empty() ? Ints{1} : degrees;
    Ints harmony;
    if (c.count() == 1 && ps != 2 && ps != 4) {
        c.starts = {0, per / 2};
        harmony = {d[0], d[1 % d.size()]};
    } else if (ps == 4) {
        c.starts = {0};
        harmony = {d[0]};
        if (c.count() > 1 && d.size() > 1) {
            c.starts.push_back((c.count() + 1) / 2 * per);
            harmony.push_back(d[1]);
        }
    } else {
        for (int i = 0; i < c.count(); ++i) {
            c.starts.push_back(i * per);
            harmony.push_back(d[size_t(i) % d.size()]);
        }
    }
    for (size_t i = 0; i < c.starts.size(); ++i) {
        c.spans.push_back((i + 1 < c.starts.size() ? c.starts[i + 1] : c.count() * per) - c.starts[i]);
        c.roots.push_back(Chords::degreeIndex(harmony[i], part.scale));
    }
    const auto fallback = defaultKick(part.style);
    for (int page : playing) c.kicks.push_back(page < int(kicks.size()) ? kicks[size_t(page)] : fallback);
    const int end = (c.count() - 1) * per + lengths[size_t(playing.back())];
    for (const auto& n : styleRole(part, c, rng)) {
        if (n.step < 0 || n.step / per >= c.count()) continue;
        const int page = playing[size_t(n.step / per)], step = n.step % per;
        const int length = lengths[size_t(page)];
        if (step >= length) continue;
        const int room = length == per ? end - n.step : length - step;
        for (int degree : n.degrees)
            out[size_t(page)].push_back({step, note(degree, part), std::max(1, std::min({n.length, room, 32})), n.velocity});
    }
    return out;
}
}
