// Порт Core/Model/ArrangeAuto.swift (ветка swift JAMbo): строки и точки автоматизации дорожки.
#include <algorithm>
#include "AnalogMod.h"
#include "Track.h"

namespace jambo {
namespace ArrAutoTarget {

const std::vector<std::string>& ids() {
    // VoiceParam.ids (SoundSnapshot.swift) + Duck, Chance, Ratchet.
    static const std::vector<std::string> v = {
        "pitch", "start", "end", "level", "attack", "decay", "filter", "reso", "pan", "harm", "timbre", "morph",
        "reverb", "delay", "drive", "p4", "rate", AnalogMod::modId, AnalogMod::octId, Params::filterTypeId,
        "sidechain", "prob", "ratchet"};
    return v;
}

int index(const std::string& id) {
    const auto& v = ids();
    const auto it = std::find(v.begin(), v.end(), id);
    return it != v.end() ? int(it - v.begin()) : -1;
}

}  // namespace ArrAutoTarget

int Track::autoLane(const std::string& id) const {
    for (size_t i = 0; i < autos.size(); ++i) if (autos[i].param == id) return int(i);
    return -1;
}

std::vector<std::vector<ArrAutoPoint>> Track::autoGroups(const std::string& id) const {
    std::vector<std::vector<ArrAutoPoint>> out;
    for (const auto& b : blocks) {
        const auto l = std::find_if(b.autos.begin(), b.autos.end(), [&](const ArrAutoLane& a) { return a.param == id; });
        if (l == b.autos.end()) continue;
        const double s = double(b.start), len = double(b.length);
        std::vector<ArrAutoPoint> cur;
        for (const auto& pt : l->points) if (pt.x >= 0 && pt.x <= len) cur.push_back({s + pt.x, pt.y});
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

std::vector<ArrAutoPoint> Track::autoPoints(const std::string& id) const {
    std::vector<ArrAutoPoint> out;
    for (const auto& g : autoGroups(id)) out.insert(out.end(), g.begin(), g.end());
    return out;
}

std::optional<double> Track::autoValue(const std::string& id, double x) const {
    for (const auto& g : autoGroups(id))
        if (auto v = ArrAutoLane::value(g, x)) return v;
    return std::nullopt;
}

void Track::setAuto(const std::string& id, const std::vector<ArrAutoPoint>& points) {
    if (ArrAutoTarget::index(id) < 0) return;
    std::vector<ArrAutoPoint> p;
    for (const auto& q : points) p.push_back({std::max(0.0, q.x), std::min(1.0, std::max(0.0, q.y))});
    std::stable_sort(p.begin(), p.end(), [](const ArrAutoPoint& a, const ArrAutoPoint& b) { return a.x < b.x; });
    if (p.size() > size_t(ArrAutoLane::maxPoints)) p.resize(size_t(ArrAutoLane::maxPoints));
    if (autoLane(id) < 0) autos.push_back({id, {}});
    std::vector<std::vector<ArrAutoPoint>> per(blocks.size());
    int last = -1;
    for (const auto& pt : p) {
        int k = -1;
        if (last >= 0 && pt.x >= double(blocks[size_t(last)].start) && pt.x <= double(blocks[size_t(last)].end())) {
            k = last;
        } else {
            for (int j = int(blocks.size()) - 1; j >= 0; --j)
                if (pt.x >= double(blocks[size_t(j)].start) && pt.x <= double(blocks[size_t(j)].end())) {
                    k = j;
                    break;
                }
        }
        if (k < 0) continue;
        last = k;
        per[size_t(k)].push_back({pt.x - double(blocks[size_t(k)].start), pt.y});
    }
    for (size_t k = 0; k < blocks.size(); ++k) {
        auto& a = blocks[k].autos;
        a.erase(std::remove_if(a.begin(), a.end(), [&](const ArrAutoLane& l) { return l.param == id; }), a.end());
        if (!per[k].empty()) a.push_back({id, per[k]});
    }
}

void Track::deleteAuto(const std::string& id) {
    auto drop = [&](std::vector<ArrAutoLane>& a) {
        a.erase(std::remove_if(a.begin(), a.end(), [&](const ArrAutoLane& l) { return l.param == id; }), a.end());
    };
    drop(autos);
    for (auto& b : blocks) drop(b.autos);
}

void Track::clearAllAutos() {
    autos.clear();
    for (auto& b : blocks) b.autos.clear();
}

void Track::migrateAutos() {
    std::vector<ArrAutoLane> old;
    for (const auto& l : autos) if (!l.points.empty()) old.push_back(l);
    if (old.empty()) return;
    for (const auto& l : old) {
        setAuto(l.param, l.points);
        for (auto& b : blocks)
            for (int e : {b.start, b.end()})
                if (auto v = l.value(double(e))) b.pinAuto(l.param, double(e - b.start), *v);
    }
    for (auto& l : autos) l.points.clear();
}

void Track::dropLegacyLinks() {
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (!blocks[i].legacyLink) continue;
        blocks[i].legacyLink = false;
        if (i + 1 >= blocks.size() || blocks[i].end() != blocks[i + 1].start) continue;
        const double e = double(blocks[i].end());
        const std::vector<ArrAutoLane> ls = blocks[i].autos;
        for (const auto& l : ls) {
            const ArrAutoPoint* a = nullptr;
            for (const auto& p : l.points) if (p.x <= double(blocks[i].length)) a = &p;
            const auto& na = blocks[i + 1].autos;
            const auto nl = std::find_if(na.begin(), na.end(), [&](const ArrAutoLane& x) { return x.param == l.param; });
            if (!a || nl == na.end()) continue;
            const auto b = std::find_if(nl->points.begin(), nl->points.end(), [](const ArrAutoPoint& p) { return p.x >= 0; });
            if (b == nl->points.end()) continue;
            const double ax = double(blocks[i].start) + a->x;
            const double bx = e + b->x;
            const double y = bx > ax ? a->y + (b->y - a->y) * (e - ax) / (bx - ax) : b->y;
            blocks[i].pinAuto(l.param, e - double(blocks[i].start), y);
            blocks[i + 1].pinAuto(l.param, 0, y);
        }
    }
}

}  // namespace jambo
