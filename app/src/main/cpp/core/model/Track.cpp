// Порт Track (Core/Model/Project.swift, ветка swift JAMbo).
#include "Track.h"
#include <random>
#include "DrumRhythm.h"
#include "SynthEngines.h"

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
    std::vector<std::optional<Clip>> c(static_cast<size_t>(Params::patternSlots));
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
    if (b >= 0) return blocks[size_t(b)].pattern.edit();
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

std::vector<SliceMark> Track::sliceMarks(const std::string& key) const {
    if (key.empty()) return {};
    const auto it = slices.find(key);
    return it != slices.end() ? it->second : std::vector<SliceMark>();
}

bool Track::hasCuts() const {
    if (sample.empty() || SynthKey::engine(sample) >= 0) return false;
    return !sliceMarks(sample).empty();
}

int Track::drumRole() const { return DrumRhythm::role(SynthKey::engine(sample)); }

}  // namespace jambo
