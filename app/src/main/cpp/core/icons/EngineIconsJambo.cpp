// Иконки синтов ANALOG, FM, WTABLE, SUB (17–19, 24) — порт EngineIconsJambo.swift (ветка swift JAMbo).
#include "EngineIcons.h"
#include "../synth/Jambo.h"
#include "../synth/Wavetable.h"
#include <cmath>
#include <cstdio>

namespace jambo {
namespace EngineIcons {

static IconLane lane3(double y0, double amp, double alpha = 1) {
    IconLane l;
    l.y0 = y0;
    l.amp = amp;
    l.alpha = alpha;
    return l;
}

Elements analogIcon(const IconValues& v) {
    const double ang = clamp(v.q) * M_PI / 2;
    const double ga = std::cos(ang), gb = std::sin(ang), mx = std::max(ga, gb);
    const double dd = v.m * v.m, sc = still * 0.16;
    static const std::vector<int> waves = {0, 1, 2, 3};
    const double kb = v.ao ? 0.5 : 1;
    const double amt = clamp(v.m);
    const int am = v.am;
    const auto a = gen([&](double u, int) { return morph(waves, v.h * 3, u * 2 - sc); });
    const auto b = gen([&](double u, int) {
        const double pa = u * 2 - sc;
        if (am == 1) {
            const double r = pa - std::floor(pa);
            return morph(waves, v.t * 3, r * kb * std::pow(2, amt * 2));
        }
        if (am == 2) {
            const double x = morph(waves, v.h * 3, pa);
            return morph(waves, v.t * 3, pa * kb + amt * amt * x);
        }
        if (am == 3) {
            const double x = morph(waves, v.h * 3, pa);
            const double y = morph(waves, v.t * 3, pa * kb);
            return y + (x * y - y) * amt;
        }
        if (am == 4) {
            const double x = morph(waves, v.h * 3, pa);
            const double y = morph(waves, v.t * 3, pa * kb);
            return y + y * amt * (x - 1) * 0.5;
        }
        return morph(waves, v.t * 3, u * 2 * kb * (1 + 0.06 * dd) + 0.15 * dd - sc);
    });
    return {lane(a, lane3(20, 10.5, 0.15 + 0.85 * ga / mx)), lane(b, lane3(44, 10.5, 0.15 + 0.85 * gb / mx))};
}

Elements fmIcon(const IconValues& v) {
    const double r = 0.5 * std::pow(16, v.h), idx = 2.5 * std::pow(clamp(v.t), 1.5);
    const double k = v.m < 0.5 ? 2 * v.m : 1 + 8 * (v.m - 0.5);
    const double dec = k * (1.6 - 1.3 * v.d);
    const double fb = v.q * 1.4, sc = still * 0.2;
    const auto modS = [&](double u) {
        const double a = tau * (u * 1.5 - sc) * r;
        return std::sin(a + fb * std::sin(a));
    };
    const auto mod = gen([&](double u, int) { return modS(u); });
    const auto car = gen([&](double u, int) {
        const double ph = u * 1.5 - sc;
        return std::sin(tau * ph + idx * std::exp(-u * dec) * modS(u));
    });
    const double sn = J::fmRatio(v.h);
    const bool whole = sn >= 1 && std::abs(sn - std::round(sn)) < 0.01;
    char buf[32];
    if (whole) std::snprintf(buf, sizeof buf, "×%d", int(std::round(sn)));
    else std::snprintf(buf, sizeof buf, "×%.1f", sn);
    return {lane(mod, lane3(13, 7, 0.5)), lane(car, lane3(38, 14)), IconElement::ofText(buf, 49, 61, 8)};
}

Elements subIcon(const IconValues& v) {
    const double q = clamp(v.q);
    const double gn = std::pow(10, 1.2 * q), wg = std::min(1.0, q / 0.1);
    const auto grit = [&](double x) { return (1 - wg) * x + (wg * std::tanh(gn * x)) / std::tanh(gn); };
    const double gb = clamp(v.t);
    const double fat = 0.12 * clamp(v.m), sc = still * 0.16, idx = v.h * 2;
    static const std::vector<int> waves = {1, 2, 3};
    const auto a = gen([&](double u, int) { return grit(morph(waves, idx, u * 2 - sc)); });
    const auto b = gen([&](double u, int) { return grit(morph(waves, idx, u + fat * u - sc / 2)); });
    return {lane(a, lane3(20, 10, 1)), lane(b, lane3(46, 10, 0.15 + 0.85 * gb))};
}

double basicFrame(double pos, double p) {
    const double pp = clamp(pos);
    if (pp < 0.5) return lerp(shSine(p), shSaw(p), sstep(pp * 2));
    return lerp(shSaw(p), shSq(p), sstep((pp - 0.5) * 2));
}

Elements wtableIcon(const IconValues& v, const Frame& frame) {
    const double pos = v.h, warp = v.t, uni = v.m, det = v.q;
    const double sc = still * 0.2, cyc = 1.5;
    const double a = clamp(warp * 2);
    const double sy = warp <= 0.5 ? 1 : 1 + (warp - 0.5) * 6;
    const auto warpP = [&](double p) { return std::pow(frac(p), 1 + 3 * a) * sy; };
    const auto mk = [&](double off) {
        return gen([&](double u, int) { return frame(pos, warpP(cyc * u - sc - off)); });
    };
    const double a3 = clamp(uni * 2), a5 = clamp(uni * 2 - 1);
    const double d = det * 0.2 * (1 + 0.12 * std::sin(still * 1.7));
    Elements els;
    const std::pair<double, double> ghosts[4] = {{d, 0.3 * a3}, {-d, 0.3 * a3}, {d / 2, 0.3 * a5}, {-d / 2, 0.3 * a5}};
    for (const auto& [off, al] : ghosts)
        if (al > 0.01) els.push_back(lane(mk(off), lane3(32, 22, al)));
    els.push_back(lane(mk(0), lane3(32, 22)));
    return els;
}

Frame tableFrame(const WtTable& tb) {
    const WtRef ref = tb.ref();
    return [ref](double pos, double p) {
        const int size = ref.sizes[0];
        const float* data = ref.data[0];
        const double fpos = clamp(pos) * double(ref.frames - 1);
        const int fi = std::min(std::max(0, ref.frames - 2), int(std::floor(fpos)));
        const int rowB = ref.frames > 1 ? fi + 1 : fi;
        const double ff = ref.frames > 1 ? fpos - double(fi) : 0;
        const double x = frac(p) * double(size);
        const int j = std::min(size - 1, int(std::floor(x)));
        const int j1 = (j + 1) % size;
        const double fj = x - double(j);
        const auto at = [&](int row, int k) { return double(data[row * size + k]); };
        const double va = at(fi, j) + (at(fi, j1) - at(fi, j)) * fj;
        const double vb = at(rowB, j) + (at(rowB, j1) - at(rowB, j)) * fj;
        return va + (vb - va) * ff;
    };
}

}  // namespace EngineIcons
}  // namespace jambo
