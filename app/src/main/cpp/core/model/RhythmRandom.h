// Случайный выбор и компактные таблицы генераторов партий.
#pragma once
#include <algorithm>
#include <iterator>
#include <random>
#include <string_view>
#include <vector>

namespace jambo::RhythmRandom {
using Rng = std::mt19937;
using Ints = std::vector<int>;

inline int integer(Rng& rng, int low, int high) {
    return std::uniform_int_distribution<int>(low, high)(rng);
}
inline double real(Rng& rng, double low = 0, double high = 1) {
    return std::uniform_real_distribution<double>(low, high)(rng);
}
inline bool coin(Rng& rng) { return integer(rng, 0, 1) != 0; }

template<class Range> decltype(auto) pick(Rng& rng, const Range& items) {
    return items[size_t(integer(rng, 0, int(std::size(items)) - 1))];
}

inline size_t weighted(Rng& rng, const std::vector<double>& weights) {
    double total = 0;
    for (double w : weights) total += w;
    double position = real(rng, 0, std::max(0.000001, total));
    for (size_t i = 0; i < weights.size(); ++i) {
        if (position < weights[i]) return i;
        position -= weights[i];
    }
    return weights.size() - 1;
}

inline std::vector<std::string_view> split(std::string_view text, char sep = ' ') {
    std::vector<std::string_view> parts;
    while (!text.empty()) {
        const auto boundary = text.find(sep);
        if (boundary != 0) parts.push_back(text.substr(0, boundary));
        if (boundary == text.npos) break;
        text.remove_prefix(boundary + 1);
    }
    return parts;
}
inline int hexDigit(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
inline Ints digits(std::string_view str) {
    Ints out;
    for (char c : str) out.push_back(hexDigit(c));
    return out;
}
}  // namespace jambo::RhythmRandom
