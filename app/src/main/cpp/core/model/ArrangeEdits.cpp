// Порт части Core/Model/ArrangeEdits.swift и правок ArrBlock, Arrangement.repair (Arrange.swift; ветка swift JAMbo).
#include <algorithm>
#include <cmath>
#include "Track.h"

namespace jambo {

void ArrBlock::shiftAutos(int d) {
    if (d == 0 || autos.empty()) return;
    for (auto& l : autos) for (auto& p : l.points) p.x -= double(d);
}

void ArrBlock::pinAuto(const std::string& param, double x, double y) {
    for (auto& l : autos) {
        if (l.param != param) continue;
        for (const auto& p : l.points) if (p.x == x) return;
        auto it = std::find_if(l.points.begin(), l.points.end(), [x](const ArrAutoPoint& p) { return p.x > x; });
        l.points.insert(it, ArrAutoPoint{x, y});
        return;
    }
    autos.push_back({param, {ArrAutoPoint{x, y}}});
}

void ArrBlock::pinCut(int x) {
    const double cx = double(x);
    const std::vector<ArrAutoLane> ls = autos;  // Swift: обход копии
    for (const auto& l : ls)
        if (auto v = ArrAutoLane::value(l.points, cx)) pinAuto(l.param, cx, *v);
}

void Arrangement::repair() {
    for (auto& m : markers) {
        m.at = std::max(0, m.at);
        // prefix(12) — по символам: UTF-8 режется по границам символов.
        size_t i = 0, n = 0;
        while (i < m.name.size() && n < 12) {
            ++i;
            while (i < m.name.size() && (static_cast<unsigned char>(m.name[i]) & 0xC0) == 0x80) ++i;
            ++n;
        }
        m.name.resize(i);
        if (m.color < 0 || m.color >= ArrPalette::count) m.color = ArrPalette::defaultColor;
        if (m.end && *m.end <= m.at) m.end.reset();
    }
    std::stable_sort(markers.begin(), markers.end(), [](const ArrMarker& a, const ArrMarker& b) { return a.at < b.at; });
    loopFrom = std::max(0, loopFrom);
    if (loopTo <= loopFrom) {
        loopTo = loopFrom;
        loop = false;
    }
    grid = std::max(0, std::min(grid, 3));
    if (ref) {
        if (ref->key.empty()) {
            ref.reset();
        } else {
            ref->offset = std::isfinite(ref->offset) ? ref->offset : 0;
            ref->gain = std::isfinite(ref->gain) ? std::max(Params::volMin, std::min(0.0, ref->gain)) : 0;
            if (ref->bpm && (!std::isfinite(*ref->bpm) || *ref->bpm < 0)) ref->bpm.reset();
        }
    }
}

void Track::clearArrange(int from, int to) {
    if (to <= from) return;
    std::vector<ArrBlock> out;
    for (const auto& b : blocks) {
        if (b.end() <= from || b.start >= to) {
            out.push_back(b);
            continue;
        }
        if (b.start < from) {
            ArrBlock left = b;
            left.length = from - b.start;
            left.pinCut(left.length);
            out.push_back(left);
        }
        if (b.end() > to) {
            const int cut = to - b.start;
            ArrBlock right = b;
            right.pinCut(cut);
            right.start = to;
            right.length = b.end() - to;
            right.offset = b.offset + cut;
            right.shiftAutos(cut);
            out.push_back(right);
        }
    }
    blocks = out;
}

int Track::placeBlock(const ArrBlock& b) {
    ArrBlock n = b;
    n.start = std::max(0, n.start);
    n.length = std::max(1, n.length);
    clearArrange(n.start, n.end());
    blocks.push_back(n);
    // Swift sort не устойчив, но блоки после clearArrange не перекрываются и с разными start — порядок один.
    std::stable_sort(blocks.begin(), blocks.end(), [](const ArrBlock& a, const ArrBlock& c) { return a.start < c.start; });
    for (size_t i = 0; i < blocks.size(); ++i) if (blocks[i] == n) return int(i);
    return 0;
}

void Track::repairBlocks() {
    if (color < 0 || color >= ArrPalette::count) color = ArrPalette::defaultColor;
    std::vector<ArrBlock> saved;
    for (const auto& b : blocks) if (b.clip >= 0 && b.clip < Params::patternSlots) saved.push_back(b);
    blocks.clear();
    std::stable_sort(saved.begin(), saved.end(), [](const ArrBlock& a, const ArrBlock& c) { return a.start < c.start; });
    for (const auto& b : saved) placeBlock(b);
}

}  // namespace jambo
