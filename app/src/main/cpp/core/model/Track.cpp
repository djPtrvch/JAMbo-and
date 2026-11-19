// Порт Track (Core/Model/Project.swift, ветка swift JAMbo).
#include "Track.h"
#include <random>

namespace jambo {

static int randomId() {
    static std::mt19937 rng{std::random_device{}()};
    return int(rng() & 0x7FFFFFFF);  // 0..<2^31
}

Track::Track(int index) : id(randomId()), name(std::to_string(index + 1)), params(Params::defaults()) {}

Track Track::channel(const std::string& name) {
    Track t(0);
    t.name = name;
    return t;
}

std::vector<std::optional<Clip>> Track::firstClips() {
    std::vector<std::optional<Clip>> c(size_t(Params::patternSlots));
    c[0] = Clip();
    return c;
}

int Track::editedBlock() const {
    if (!editBlock) return -1;
    const int i = *editBlock;
    return i >= 0 && i < int(blocks.size()) && blocks[size_t(i)].pattern ? i : -1;
}

const Clip& Track::openClip() const {
    static const Clip none;
    const int b = editedBlock();
    if (b >= 0) return *blocks[size_t(b)].pattern;
    const auto& c = clips[size_t(openIndex())];
    return c ? *c : none;
}

Clip& Track::editOpen() {
    const int b = editedBlock();
    if (b >= 0) return *blocks[size_t(b)].pattern;
    auto& c = clips[size_t(openIndex())];
    if (!c) c = Clip();
    return *c;
}

double Track::value(const std::string& id, const Step* step) const {
    if (step) {
        const auto it = step->locks.find(id);
        if (it != step->locks.end()) return it->second;
    }
    const auto it = params.find(id);
    if (it != params.end()) return it->second;
    const ParamDef* p = Params::byId(id);
    return p ? p->def : 0;
}

}  // namespace jambo
