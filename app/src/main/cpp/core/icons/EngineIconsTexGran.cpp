// Иконки TEXTURE (25) и GRANULAR (26) — порт EngineIconsTexGran.swift (ветка swift JAMbo).
#include "EngineIcons.h"
#include "../synth/TexGran.h"
#include "../synth/TextureVoice.h"
#include <cmath>

namespace jambo {
namespace EngineIcons {

std::vector<double> iconEvents(double rate, double w, double seed, int max) {
    std::vector<double> out;
    double t = 0.003;
    int k = 0;
    while (t < w && int(out.size()) < max) {
        out.push_back(t);
        const double u = std::min(0.999, hash(double(k) * 1.37 + seed));
        t += std::max(0.0005, -std::log(1 - u) / rate);
        ++k;
    }
    return out;
}

Elements textureIcon(const IconValues& v) {
    const double c0 = clamp(v.h), g0 = clamp(v.t), tone = clamp(v.m), metal = clamp(v.q);
    const double w = texWindow;
    const auto ev = iconEvents(TextureMap::rate(g0) * 1.5, w, 3.1, 120);
    std::vector<double> amps;
    for (size_t k = 0; k < ev.size(); ++k) amps.push_back(0.5 + 0.5 * hash(double(k) * 2.71 + 5));
    const double lv = std::max(TextureMap::length(g0), 0.006);
    const double crack = TextureMap::gain(g0) / 2.2;
    const double dry = std::cos(tone * M_PI / 2), wet = std::sin(tone * M_PI / 2);
    double ws[4], ra[4], wsum = 0;
    for (int i = 0; i < 4; ++i) {
        ws[i] = TextureMap::weight(i, c0);
        ra[i] = TextureMap::ratio(i, metal);
        wsum += ws[i];
    }
    const double fine = 0.12 * std::pow(8.3, c0);
    const auto a = gen([&](double u, int i) {
        const double t = u * w;
        double e = 0;
        for (size_t k = 0; k < ev.size(); ++k) {
            const double tk = ev[k];
            if (tk > t) break;
            const double dt = t - tk;
            if (dt < 2 * lv + 0.004) e += amps[k] * std::exp(-dt / (lv / 3));
        }
        const double n = nzi(double(i) * fine + 11);
        const double ph = tau * 6 * u;
        double s1 = 0;
        for (int k = 0; k < 4; ++k) s1 += ws[k] * std::sin(ph * ra[k]);
        const double level = 0.45 + crack * std::min(1.0, e) * 0.55;
        const double x = (dry * n + wet * s1 / wsum) * level;
        return clamp(x * 1.3, -1, 1);
    });
    IconLane l;
    l.y0 = 32;
    l.amp = 26;
    return {lane(a, l)};
}

Elements granularIcon(const IconValues& v, const std::vector<double>* peaks) {
    const double pos = clamp(v.h), size = clamp(v.t), dens = clamp(v.m), sp = clamp(v.q);
    const double span = x1 - x0;
    const double gw = (2 + 14 * size) / span;
    const int n = 2 + int(std::round(dens * 14));
    const double half = TexGran::posSpread(sp);
    std::vector<double> starts;
    for (int k = 0; k < n; ++k) {
        const double x = pos + rnd(k, 7.7) * half;
        starts.push_back(clamp(x, 0, std::max(0.0, 1 - gw)));
    }
    const std::vector<double>* real = peaks && peaks->size() == size_t(ns) + 1 ? peaks : nullptr;
    const auto wave = [&](double u, int i) {
        if (real) return (*real)[size_t(i)];
        return TexGranIcon::sampleEnv(u) * (0.55 + 0.45 * std::abs(nz(i * 5 + 3)));
    };
    const auto smp = gen([&](double u, int i) { return wave(u, i); });
    const auto grains = gen([&](double u, int i) {
        double g = 0;
        for (double s0 : starts) {
            const double x = (u - s0) / gw;
            if (x <= 0 || x >= 1) continue;
            g += 0.5 * (1 - std::cos(tau * x));
        }
        return wave(u, i) * std::min(1.0, g);
    });
    const double lo = clamp(pos - half), hi = clamp(pos + half);
    const double xa = x0 + span * lo, xp = x0 + span * pos;
    const double za = std::max(x0, std::min(xa, xp - 1));
    const double zb = std::min(x1, std::max(x0 + span * hi, x0 + span * (pos + gw)));
    std::vector<IconRect> ticks;
    for (int k = 0; k < n; ++k) {
        const double f = (double(k) + 0.5 + 0.35 * rnd(k, 3.3)) / double(n);
        const double x = za + (zb - za) * clamp(f);
        const double h = 3 + 3 * hash(double(k) * 1.9 + 4);
        ticks.push_back({x - 0.4, 3, 0.8, h});
    }
    IconLane ls;
    ls.y0 = 34;
    ls.amp = 22;
    ls.mirror = true;
    ls.ink = IconInk::secondary;
    IconLane lg = ls;
    lg.thin = true;
    lg.ink = IconInk::primary;
    return {
        lane(smp, ls),
        IconElement::ofRects({{za, 2, std::max(0.8, zb - za), 60}}, IconInk::primary, 0.18),
        lane(grains, lg),
        IconElement::ofRects(std::move(ticks), IconInk::primary, 0.8),
        IconElement::ofRects({{xp - 0.5, 2, 1, 60}}),
    };
}

}  // namespace EngineIcons

namespace TexGranIcon {
double smooth(double a, double b, double x) {
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}

double sampleEnv(double p) {
    const double x = std::min(1.0, std::max(0.0, p));
    double e = 0.74 + 0.14 * std::sin(EngineIcons::tau * 1.3 * x + 0.4) + 0.09 * std::sin(EngineIcons::tau * 3.7 * x + 1.1);
    e += 0.06 * EngineIcons::nzi(x * 40 + 5);
    const double edge = std::min(1.0, std::min(x, 1 - x) / 0.04);
    return std::min(1.0, std::max(0.45, e)) * (0.3 + 0.7 * edge);
}
}  // namespace TexGranIcon

}  // namespace jambo
