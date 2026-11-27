// Рисунки ударных и p-lock по стилю проекта.
#include "DrumRhythm.h"
#include <string_view>

namespace jambo::DrumRhythm {

namespace {
using Rng = std::mt19937;

int between(Rng& rng, int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

bool chance(Rng& rng, int percent) {
    return between(rng, 1, 100) <= percent;
}

std::string_view variant(std::string_view table, Rng& rng) {
    int index = between(rng, 0, int(std::count(table.begin(), table.end(), ' ')));
    while (index-- > 0) table.remove_prefix(table.find(' ') + 1);
    return table.substr(0, table.find(' '));
}

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


}  // namespace

std::vector<int> drumBase(int role, int style, int len, Rng& rng) {
    std::vector<int> hits;
    if (role == DrumRhythm::perc) {
        constexpr int low[] = {3, 3, 3, 2, 2, 2}, high[] = {5, 5, 7, 5, 4, 5};
        const int count = std::min(len, between(rng, low[style], high[style]));
        const int offset = between(rng, 0, len - 1);
        for (int i = 0; i < count; ++i) hits.push_back((i * len / count + offset) % len);
        std::sort(hits.begin(), hits.end());
    } else {
        const auto pattern = variant(role == DrumRhythm::kick ? kickPatterns[style]
            : role == DrumRhythm::hat ? hatPatterns[style] : snarePatterns[style], rng);
        for (int step = 0; step < std::min(len, int(pattern.size())); ++step) {
            const char cell = pattern[size_t(step)];
            if (cell == 'x' || (cell >= '1' && cell <= '9' && chance(rng, (cell - '0') * 10)))
                hits.push_back(step);
        }
    }
    if (hits.empty()) hits.push_back(len / 2);
    return hits;
}

void writeDrums(Page& page, const std::vector<int>& base, int role, int style, int note,
                int len, bool vary, bool fill, Rng& rng) {
    auto hits = base;
    hits.erase(std::lower_bound(hits.begin(), hits.end(), len), hits.end());
    if (vary) {
        const int s = between(rng, 0, len - 1);
        const auto it = std::lower_bound(hits.begin(), hits.end(), s);
        if (it == hits.end() || *it != s) hits.insert(it, s);
        else if (hits.size() > 1 && (role != DrumRhythm::kick || s != 0) &&
                 ((role != DrumRhythm::snare && role != DrumRhythm::clap) || s % 4 != 0)) hits.erase(it);
    }
    if (fill) {
        constexpr int probability[] = {40, 60, 70, 60, 50};
        const int start = std::max(0, len - 4);
        hits.erase(std::lower_bound(hits.begin(), hits.end(), start), hits.end());
        const auto prior = hits.size();
        for (int s = start; s < len; ++s)
            if (chance(rng, probability[role])) hits.push_back(s);
        if (hits.size() == prior) hits.push_back(between(rng, start, len - 1));
    }
    if (hits.empty()) hits.push_back(len / 2);
    for (int s : hits) {
        Step& step = page[size_t(s)];
        step.on = true;
        step.notes = {note};
        const bool backbeat = (role == DrumRhythm::snare || role == DrumRhythm::clap) && s % 4 != 0;
        if (role != DrumRhythm::kick)
            step.locks["level"] = s % 4 == 0 ? 127 : backbeat ? between(rng, 38, 57) : between(rng, 89, 108);
        if (style == 3 && role == DrumRhythm::hat && s % 4 != 0 && chance(rng, 20))
            step.locks["ratchet"] = between(rng, 1, 3);
        if (s > 0 && style == 5 && role != DrumRhythm::kick) step.locks["prob"] = 50;
        if (s > 0 && style == 1 && role == DrumRhythm::hat && s % 4 != 2) step.locks["prob"] = 75;
    }
}

}  // namespace jambo::DrumRhythm
