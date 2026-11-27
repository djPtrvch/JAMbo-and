// Рисунки ударных и p-lock по стилю проекта.
#include "DrumRhythm.h"
#include <cmath>

namespace jambo::DrumRhythm {

namespace {
using namespace RhythmRandom;

// Рисунки ударных из DrumRhythm.swift: x — обязательный шаг, цифра — вероятность из десяти.
constexpr const char* kickPatterns[] = {
    "x...x...x...x..2 x..2x...x.2.x...",
    "x...x...x...x... x...x...x...x.2. x..1x...x..1x...",
    "x.6.......x5.... x.3....3..7.....", "x......4..53..3. x.....45...5....",
    "x......2x.4..... x...x...x...x... x.....5.x.......", "x.........2..... x...............",
};
constexpr const char* snarePatterns[] = {
    "...1x.1..1.1x.11", "....x..1....x..1 .......1....x..1", "....x..4.4..x.44",
    "...1..1.x..1..11", "...1x.1..1.1x.11", "............x...",
};
constexpr const char* hatPatterns[] = {
    "22x222x222x222x2 66x666x666x666x6", "55x555x555x555x5 88x888x888x888x8 ..7...7...7...7.",
    "8282828282828282", "x4x4x4x4x4x4x4x4", "x1x1x1x1x1x1x1x1", "3333333333333333",
};
const Ints snareGhosts[] = {{3, 6, 9, 11, 14, 15}, {7, 15}, {7, 9, 14, 15},
                           {3, 6, 11, 14, 15}, {3, 6, 9, 11, 14, 15}, {}};

bool snareLike(int role) { return role == snare || role == clap; }

Hit hit(int style, int role, int step, bool ghost, Rng& rng) {
    Hit out{step, {}, {}, {}};
    if (role != kick) {
        out.velocity = ghost ? std::round(real(rng, 38.1, 57.15))
            : step % 4 == 0 ? 127.0 : std::round(real(rng, 88.9, 107.95));
    }
    if (style == 3 && role == hat && step % 4 != 0 && real(rng) < 0.2) out.ratchet = integer(rng, 1, 3);
    if (step > 0 && style == 5 && role != kick) out.chance = 50;
    if (step > 0 && style == 1 && role == hat && step % 4 != 2) out.chance = 75;
    return out;
}

Hits base(int role, int style, int length, Rng& rng) {
    Hits out;
    if (role == perc) {
        constexpr int low[] = {3, 3, 3, 2, 2, 2}, high[] = {5, 5, 7, 5, 4, 5};
        const int count = integer(rng, low[style], high[style]);
        const int shift = integer(rng, 0, length - 1);
        for (int step : euclid(count, length, shift)) out.push_back(hit(style, role, step, false, rng));
    } else {
        const auto options = split(role == kick ? kickPatterns[style]
            : role == hat ? hatPatterns[style] : snarePatterns[style]);
        const auto cells = options.size() == 1 ? options.front() : pick(rng, options);
        const auto& ghosts = snareGhosts[style];
        for (int step = 0; step < length; ++step) {
            const char cell = step < int(cells.size()) ? cells[size_t(step)] : '.';
            const double p = cell == 'x' ? 1 : cell == '.' ? 0 : (cell - '0') / 10.0;
            if (p > 0 && real(rng) < p) {
                const bool ghost = snareLike(role) && p < 1 && std::find(ghosts.begin(), ghosts.end(), step) != ghosts.end();
                out.push_back(hit(style, role, step, ghost, rng));
            }
        }
    }
    if (out.empty()) out.push_back(hit(style, role, length / 2, false, rng));
    return out;
}

bool anchor(int role, int style, int step) {
    if (role == kick) return step == 0;
    if (!snareLike(role)) return false;
    const auto& ghosts = snareGhosts[style];
    if (std::find(ghosts.begin(), ghosts.end(), step) != ghosts.end()) return false;
    const auto options = split(snarePatterns[style]);
    return std::all_of(options.begin(), options.end(), [step](auto row) {
        return step < int(row.size()) && row[size_t(step)] == 'x';
    });
}

Hits vary(const Hits& source, int role, int style, int length, Rng& rng) {
    Hits out;
    for (const auto& h : source) if (h.step < length) out.push_back(h);
    const int changes = integer(rng, 1, 2);
    for (int i = 0; i < changes; ++i) {
        std::vector<size_t> removable;
        for (size_t j = 0; j < out.size(); ++j) if (!anchor(role, style, out[j].step)) removable.push_back(j);
        Ints free;
        for (int s = 0; s < length; ++s)
            if (std::none_of(out.begin(), out.end(), [s](const auto& h) { return h.step == s; })) free.push_back(s);
        const bool remove = !removable.empty() && (free.empty() || coin(rng));
        if (remove) {
            const auto index = pick(rng, removable);
            if (out.size() > 1) { out.erase(out.begin() + index); continue; }
        }
        if (!free.empty()) {
            const int s = pick(rng, free);
            out.push_back(hit(style, role, s, snareLike(role) && s % 4 != 0, rng));
        }
    }
    if (out.empty()) return base(role, style, length, rng);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.step < b.step; });
    return out;
}

Hits fill(const Hits& source, int role, int style, int length, Rng& rng) {
    const int first = std::max(0, length - 4);
    constexpr double chance[] = {0.4, 0.6, 0.7, 0.6, 0.5};
    Hits out;
    for (const auto& h : source) if (h.step < first) out.push_back(h);
    const auto before = out.size();
    for (int s = first; s < length; ++s)
        if (real(rng) < chance[role]) out.push_back(hit(style, role, s, false, rng));
    if (out.size() == before) out.push_back(hit(style, role, integer(rng, first, length - 1), false, rng));
    return out;
}
}  // namespace

Ints euclid(int k, int n, int rotation) {
    Ints out;
    if (n <= 0 || k <= 0) return out;
    k = std::min(k, n);
    for (int i = 0; i < k; ++i) out.push_back(((i * n / k + rotation) % n + n) % n);
    std::sort(out.begin(), out.end());
    return out;
}

Ints steps(const Hits& hits) {
    Ints out;
    for (const auto& h : hits) out.push_back(h.step);
    return out;
}

Pattern pattern(int role, int style, const Ints& lengths, const Ints* avoid, Rng& rng) {
    Pattern out(lengths.size());
    std::vector<size_t> playing;
    for (size_t i = 0; i < lengths.size(); ++i) if (lengths[i] > 0) playing.push_back(i);
    if (playing.empty()) return out;
    style = styleIndex(style);
    const auto first = playing.front(), last = playing.back();
    out[first] = base(role, style, lengths[first], rng);
    for (int tries = 1; avoid && steps(out[first]) == *avoid && tries < 16; ++tries)
        out[first] = base(role, style, lengths[first], rng);
    for (const auto i : playing)
        if (i != first && i != last) out[i] = vary(out[first], role, style, lengths[i], rng);
    if (last != first) out[last] = fill(out[first], role, style, lengths[last], rng);
    return out;
}

void writePage(Page& page, const Hits& hits, int note) {
    for (const auto& h : hits) {
        Step& step = page[size_t(h.step)];
        step.on = true;
        step.notes = {note};
        if (h.velocity) step.locks["level"] = *h.velocity;
        if (h.ratchet) step.locks["ratchet"] = *h.ratchet;
        if (h.chance) step.locks["prob"] = *h.chance;
    }
}
}  // namespace jambo::DrumRhythm
