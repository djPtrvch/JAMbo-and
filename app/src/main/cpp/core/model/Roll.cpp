// Порт Core/Model/Roll.swift и Roll.snap/snapLen (RollEdits.swift), ветка swift JAMbo.
#include "Roll.h"
#include <algorithm>
#include <cmath>
#include "Scales.h"

namespace jambo {
namespace Roll {

const std::vector<int> grids = {1, 2, 4};
const std::vector<std::string> gridNames = {"1/16", "1/8", "1/4"};

std::vector<int> rows(int scale, int root) {
    const std::vector<int>& steps = Scales::all[size_t(Scales::wrap(scale, int(Scales::all.size())))].steps;
    std::vector<int> out;
    for (int n = 0; n <= 127; ++n)
        if (std::find(steps.begin(), steps.end(), Scales::wrap(n - root, 12)) != steps.end()) out.push_back(n);
    return out;
}

bool isSharp(int n) {
    const int c = Scales::wrap(n, 12);
    return c == 1 || c == 3 || c == 6 || c == 8 || c == 10;
}

bool isRoot(int n, int root) { return Scales::wrap(n - root, 12) == 0; }

std::vector<int> positions(const Clip& clip) {
    const int n = clip.playLen();
    std::vector<int> out;
    out.reserve(size_t(std::max(0, n)));
    for (int i = 0; i < n; ++i) out.push_back(clip.playIndex(i));
    return out;
}

std::vector<RollNote> notes(const Clip& clip) {
    std::vector<RollNote> out;
    const std::vector<int> pos = positions(clip);
    for (int p = 0; p < int(pos.size()); ++p) {
        const Step* s = clip.step(pos[size_t(p)]);
        if (!s || !s->on) continue;
        for (int k = 0; k < int(s->notes.size()); ++k)
            out.push_back({p, pos[size_t(p)], k, s->notes[size_t(k)], s->noteLen(k), s->noteVel(k)});
    }
    return out;
}

std::string beatLabel(int pos) { return std::to_string(pos / 16 + 1) + "." + std::to_string((pos % 16) / 4 + 1); }

int snap(int n, int grid) {
    const int g = std::max(1, grid);
    return (n / g) * g;
}

int snapLen(double steps, int grid, int pos, int cols) {
    const double g = double(std::max(1, grid));
    // Swift .rounded() — к ближайшему, половина от нуля (std::round).
    int n = int(std::round(steps / g)) * int(g);
    if (n < 1) n = int(g);
    return std::max(1, std::min(n, cols - pos));
}

}  // namespace Roll
}  // namespace jambo
