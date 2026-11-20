// Иконки ударных KICK, SNARE, HAT, CLAP, PERC (16, 20–23) — порт EngineIconsDrums.swift (ветка swift JAMbo).
#include "EngineIcons.h"
#include "../synth/Jambo.h"
#include <cmath>

namespace jambo {
namespace EngineIcons {

double drumEnv(double u, double d) { return std::pow(clamp(1 - u / lerp(0.35, 1, d)), 1.6); }

Elements drumBurst(double y0, double amp, double a, double w) {
    if (a < 0.01) return {};
    const auto f = [&](double x) {
        return a * amp * (1 - std::exp(-(x - x0) / 0.5)) * std::exp(-std::pow((x - x0) / w, 1.3));
    };
    std::vector<IconCmd> cmds{IconCmd::mv(x0, y0)};
    for (int i = 0; i <= 40; ++i) {
        const double x = x0 + (5 * w * double(i)) / 40;
        cmds.push_back(IconCmd::ln(x, std::max(1.5, y0 - f(x))));
    }
    for (int i = 40; i >= 0; --i) {
        const double x = x0 + (5 * w * double(i)) / 40;
        cmds.push_back(IconCmd::ln(x, std::min(62.5, y0 + f(x))));
    }
    cmds.push_back(IconCmd::cl());
    return {IconElement::path(std::move(cmds), std::nullopt, true, false, IconInk::secondary)};
}

static Elements join(Elements a, const Elements& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Elements kickIcon(const IconValues& v, std::optional<double> hx) {
    const double cyc = 1.6 * std::pow(2.5, v.h), gg = 1 + 7 * clamp(v.m), nrm = std::tanh(gg);
    double ph = 0;
    const auto a = gen([&](double u, int) {
        ph += (cyc * (1 + 7 * v.t * std::exp(-u * 12)) * tau) / ns;
        return (std::tanh(gg * std::sin(ph)) / nrm) * std::pow(clamp(1 - u / lerp(0.3, 1, v.d)), 1.3);
    });
    IconLane l;
    l.y0 = 32;
    l.amp = 25;
    l.thin = true;
    return join(join({axis(32)}, drumBurst(32, 25, 1.35 * v.q, 2.6)), {lane(a, l, hx)});
}

Elements snareIcon(const IconValues& v, std::optional<double> hx) {
    const double wb = std::cos(v.t * M_PI / 2), wn = std::sin(v.t * M_PI / 2);
    const double cyc = 3 * std::pow(3, v.h), hold = 8 * std::pow(1.2 / 8, clamp(v.m));
    double ph = 0;
    const auto a = gen([&](double u, int i) {
        ph += (cyc * (1 + 2.5 * v.q * std::exp(-u * 20)) * tau) / ns;
        const double nn = 0.85 * clamp(1.2 * nzi(double(i) / hold), -1, 1);
        return clamp(wb * std::sin(ph) + wn * nn, -1, 1) * drumEnv(u, v.d);
    });
    IconLane l;
    l.y0 = 32;
    l.amp = 25;
    l.thin = true;
    return join(join({axis(32)}, drumBurst(32, 25, 1.2 * v.q, 2.6)), {lane(a, l, hx)});
}

Elements percIcon(const IconValues& v, std::optional<double> hx) {
    const double cyc = 2.5 * std::pow(3.2, v.h), x = clamp(v.t);
    const double rip = x < 0.5 ? lerp(0.05, 0.75, x * 2) : 0.75;
    const double rk = x < 0.5 ? lerp(10, 8, x * 2) : lerp(8, 0.8, (x - 0.5) * 2);
    const double r1 = x < 0.5 ? lerp(2.3, 5.4, x * 2) : lerp(5.4, 3.7, (x - 0.5) * 2);
    const double r2 = x < 0.5 ? lerp(2.9, 8.9, x * 2) : lerp(8.9, 5.3, (x - 0.5) * 2);
    const double att = 0.16 * std::pow(1 - clamp(v.q), 1.5);
    double ph = 0;
    const auto a = gen([&](double u, int) {
        ph += (cyc * (1 + 1.6 * v.m * std::exp(-u * 9)) * tau) / ns;
        const double r = rip * std::exp(-u * rk) * (0.6 * std::sin(r1 * ph + 0.7) + 0.4 * std::sin(r2 * ph + 1.9));
        const double sw = att > 0.001 ? std::sin((M_PI / 2) * clamp(u / att)) : 1;
        return clamp((std::sin(ph) + r) / (1 + 0.5 * rip), -1, 1) * drumEnv(u, v.d) * sw;
    });
    IconLane l;
    l.y0 = 32;
    l.amp = 25;
    l.thin = true;
    return join(join({axis(32)}, drumBurst(32, 25, lerp(0.45, 1.2, v.q), lerp(9, 2, v.q))), {lane(a, l, hx)});
}

Elements hatIcon(const IconValues& v, std::optional<double> hx) {
    static const double base[6] = {205.3, 304.4, 369.6, 522.7, 540, 800};
    const double spread = 2 * clamp(v.t), shift = (2 * clamp(v.h) - 1) * 0.5;
    const double cc = 32 + 20 * (2 * clamp(v.m) - 1);
    std::vector<IconCmd> hump{IconCmd::mv(4, 59)};
    for (int i = 0; i <= 56; ++i) {
        const double x = 4 + double(i);
        const double z = (x - cc) / 8;
        hump.push_back(IconCmd::ln(x, 59 - 3 - 11 * std::exp(-z * z)));
    }
    hump.push_back(IconCmd::ln(60, 59));
    hump.push_back(IconCmd::cl());
    std::vector<IconRect> lines;
    const double q = clamp(v.q);
    const double bottom = 46.0, hh = 8 + 30 * clamp(v.d), top = bottom - hh, len = 3 + 6 * (1 - q);
    for (int i = 0; i < 6; ++i) {
        const double lx = std::log2(base[i] / J::hatMean) * spread * 0.9 + shift;
        const double x = 32 + 22 * std::max(-1.0, std::min(1.0, lx / 1.4));
        double sw = 0;
        if (hx) {
            const double z = (x - *hx) / 7;
            sw = 3 * std::exp(-z * z);
        }
        double y = top - sw;
        int j = 0;
        while (y < bottom) {
            const double dx = rnd(i * 40 + j, 11) * 3 * q;
            const double y1 = std::min(bottom, y + len);
            lines.push_back({x + dx - 0.6, y, 1.2, y1 - y + 0.05});
            y += len + 2 * q;
            ++j;
        }
    }
    return {IconElement::path(std::move(hump), std::nullopt, true, false, IconInk::secondary),
            IconElement::ofRects(std::move(lines))};
}

Elements clapIcon(const IconValues& v, std::optional<double> hx) {
    const double nEff = 1 + 5 * clamp(v.t);
    const double gap0 = 3 + 6 * std::pow(clamp(v.m), 1.2);
    const double room = 30 / std::max(1.0, nEff * 3 + (nEff - 1) * gap0 * 1.4);
    const double gap = gap0 * std::min(1.0, room);
    const double jit = v.m > 0.6 ? (0.4 * (v.m - 0.6)) / 0.4 : 0;
    const double w = 3 - 2 * clamp(v.h);
    std::vector<double> xs, ends{5.5};
    for (int k = 0; k < 6; ++k) {
        xs.push_back(ends[size_t(k)]);
        ends.push_back(ends[size_t(k)] + w + gap * (1 + jit * rnd(k, 5)));
    }
    const int fl = int(std::floor(nEff));
    const double fr = nEff - double(fl);
    const double xT = lerp(ends[size_t(fl)], ends[size_t(std::min(6, fl + 1))], fr);
    const double len = 10 + 40 * clamp(v.d), tb = 0.45 * w + 0.35;
    const auto burst = gen([&](double u, int) {
        const double x = x0 + u * (x1 - x0);
        double env = 0;
        for (int k = 0; k < 6; ++k) {
            const double p = x - xs[size_t(k)];
            if (p < 0) continue;
            const double ak = sstep(clamp(nEff - double(k))) * (1 - 0.08 * double(k));
            if (ak > 0) env = std::max(env, ak * std::exp(-p / tb));
        }
        return env;
    });
    const auto tail = gen([&](double u, int) {
        const double x = x0 + u * (x1 - x0);
        return x >= xT ? 0.92 * v.q * std::exp((-4 * (x - xT)) / len) : 0;
    });
    IconLane lt;
    lt.y0 = 32;
    lt.amp = 26;
    lt.mirror = true;
    lt.thin = true;
    lt.ink = IconInk::secondary;
    IconLane lb = lt;
    lb.ink = IconInk::primary;
    return {axis(32), lane(tail, lt, hx), lane(burst, lb, hx)};
}

}  // namespace EngineIcons
}  // namespace jambo
