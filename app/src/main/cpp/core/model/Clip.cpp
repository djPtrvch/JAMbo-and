// Порт Clip (Core/Model/Project.swift, ветка swift JAMbo).
#include "Clip.h"
#include <algorithm>
#include "../sequencer/SeqData.h"

namespace jambo {

const std::vector<int>& Clip::rates() {
    static const std::vector<int> v = {1, 0, -1, -2, -3, -4};
    return v;
}
const std::vector<std::string>& Clip::rateNames() {
    static const std::vector<std::string> v = {"2", "1", "1/2", "1/4", "1/8", "1/16"};
    return v;
}

int Clip::passTicks() const { return SeqData::passTicks(playLen(), speed()); }

int Clip::localStep(int o) const {
    const int pass = passTicks();
    const int r = ((o % pass) + pass) % pass;
    if (speed() > 0) return r * 2;
    return r >> (-speed());
}

PageRange Clip::range() const {
    const int to = std::max(0, std::min(playTo, int(pages.size()) - 1));
    return {std::min(std::max(0, playFrom), to), to};
}

int Clip::pageLen(int p, int from) const {
    const int n = Params::stepsPerPage;
    if (length > from * n) return std::max(0, std::min(n, length - p * n));
    return n;
}

int Clip::playLen() const {
    const PageRange r = range();
    return playLen(r.lower, r.upper);
}

int Clip::playLen(int from, int to) const {
    int n = 0;
    for (int p = from; p <= std::max(from, to); ++p) n += pageLen(p, from);
    return std::max(1, n);
}

int Clip::playIndex(int n) const {
    const PageRange r0 = range();
    const int len = playLen();
    int r = ((n % len) + len) % len;
    for (int p = r0.lower; p <= r0.upper; ++p) {
        const int l = pageLen(p, r0.lower);
        if (r < l) return p * Params::stepsPerPage + r;
        r -= l;
    }
    return r0.lower * Params::stepsPerPage;
}

const Step* Clip::step(int index) const {
    const int p = index / Params::stepsPerPage;
    if (index < 0 || p >= int(pages.size())) return nullptr;
    return &pages[size_t(p)][size_t(index % Params::stepsPerPage)];
}

bool Clip::outside(int index) const {
    return index % Params::stepsPerPage >= pageLen(index / Params::stepsPerPage, range().lower);
}

}  // namespace jambo
