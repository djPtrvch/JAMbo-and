// Порт части EqSpec (Core/EQ/EqModel.swift, ветка swift JAMbo).
#include "EqSnapshot.h"
#include <cmath>
#include "model/Project.h"

namespace jambo {
namespace EqSpec {

static_assert(channels == Params::maxTracks + 1 && masterChannel == Params::maxTracks, "каналы EQ");

bool dynOn(const EqBand& b) { return hasGain(b.type) && b.dyn >= 0; }

int stages(const EqBand& b) {
    return isCut(b.type) ? slopeDb(std::max(0, std::min(b.slope, slopeCount - 1))) / 12 : 1;
}

double filterQ(const EqBand& b) { return isCut(b.type) ? 20 * std::log10(b.q) : b.q; }

namespace {
/// Источники-дорожки (не свои) включённых динамических полос дорожки t.
std::vector<int> deps(const Project& project, int t, int count) {
    std::vector<int> out;
    const Eq& e = project.tracks[size_t(t)].eq;
    if (!e.on) return out;
    for (const auto& ob : e.bands) {
        if (!ob) continue;
        const EqBand& b = *ob;
        if (b.on && dynOn(b) && b.dyn >= 1 && b.dyn - 1 < count && b.dyn - 1 != t) out.push_back(b.dyn - 1);
    }
    return out;
}
}  // namespace

bool listensToTrack(const Project& project, int t, int count) { return !deps(project, t, count).empty(); }

std::vector<int> mixOrder(const Project& project, int count) {
    std::vector<std::vector<int>> d(size_t(std::max(0, count)));
    for (int t = 0; t < count; ++t) d[size_t(t)] = deps(project, t, count);
    std::vector<bool> done(size_t(std::max(0, count)), false);
    std::vector<int> order;
    while (int(order.size()) < count) {
        int pick = -1;
        for (int t = 0; t < count && pick < 0; ++t) {
            if (done[size_t(t)]) continue;
            bool ready = true;
            for (int s : d[size_t(t)]) ready = ready && done[size_t(s)];
            if (ready) pick = t;
        }
        if (pick < 0) {
            pick = 0;
            for (int t = 0; t < count; ++t) if (!done[size_t(t)]) { pick = t; break; }
        }
        done[size_t(pick)] = true;
        order.push_back(pick);
    }
    return order;
}

void fill(double* p, const Eq* eq) {
    std::fill(p, p + stride, 0.0);
    if (!eq) return;
    p[0] = eq->on ? 1 : 0;
    for (int k = 0; k < bands; ++k) {
        const int o = 1 + k * bandStride;
        const std::optional<EqBand> ob = eq->band(k);
        if (!ob || !ob->on || !eq->on) {
            p[o] = -1;
            continue;
        }
        const EqBand& b = *ob;
        p[o] = double(b.type);
        p[o + 1] = double(stages(b));
        p[o + 2] = b.freq;
        p[o + 3] = hasGain(b.type) ? b.gain : 0;
        p[o + 4] = filterQ(b);
        p[o + 5] = double(std::max(0, std::min(b.ms, msCount - 1)));
        p[o + 6] = dynOn(b) && b.dyn <= Params::maxTracks ? double(b.dyn) : -1;
        p[o + 7] = b.thr;
        p[o + 8] = b.range;
    }
}

}  // namespace EqSpec
}  // namespace jambo
