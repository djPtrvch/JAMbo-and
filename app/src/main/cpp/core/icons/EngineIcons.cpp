// Иконки: типы, выбор по движку, колонки — общий язык превью (порт EngineIcons.swift и EngineIconsColumns.swift,
// ветка swift JAMbo): сигнал считается в 72 · 6 + 1 точках, каждая из 72 колонок — прямоугольник от min до max.
#include "EngineIcons.h"
#include <cmath>

namespace jambo {

IconElement IconElement::path(std::vector<IconCmd> c, std::optional<double> op, bool fill, bool dash, IconInk ink) {
    IconElement e;
    e.cmds = std::move(c);
    e.opacity = op;
    e.fill = fill;
    e.dash = dash;
    e.ink = ink;
    return e;
}

IconElement IconElement::ofRects(std::vector<IconRect> r, IconInk ink, std::optional<double> op) {
    IconElement e;
    e.rects = std::move(r);
    e.ink = ink;
    e.opacity = op;
    e.fill = true;
    return e;
}

IconElement IconElement::ofText(std::string s, double x, double y, double size) {
    IconElement e;
    e.text = std::move(s);
    e.textX = x;
    e.textY = y;
    e.textSize = size;
    e.fill = true;
    return e;
}

namespace EngineIcons {

Elements elements(int engine, const IconValues& v, std::optional<double> hx) {
    switch (engine) {
    case 16: return kickIcon(v, hx);
    case 17: return analogIcon(v);
    case 18: return fmIcon(v);
    case 19: return wtableIcon(v, basicFrame);
    case 20: return snareIcon(v, hx);
    case 21: return hatIcon(v, hx);
    case 22: return clapIcon(v, hx);
    case 23: return percIcon(v, hx);
    case 24: return subIcon(v);
    case 25: return textureIcon(v);
    case 26: return granularIcon(v);
    default: return {};
    }
}

// MARK: — шум

double hash(double n) {
    const double v = std::sin(n * 127.1 + 311.7) * 43758.5453;
    return v - std::floor(v);
}

double rnd(int i, double seed) { return hash(double(i) * 1.713 + seed * 9.31) * 2 - 1; }

static const std::vector<double>& noise() {
    static const std::vector<double> t = [] {
        std::vector<double> v(512);
        for (int i = 0; i < 512; ++i) v[size_t(i)] = hash(double(i) * 3.17 + 7) * 2 - 1;
        return v;
    }();
    return t;
}

double nz(int i) { return noise()[size_t(((i % 512) + 512) % 512)]; }

double nzi(double x) {
    const double fi = std::floor(x);
    const int i = int(fi);
    return lerp(nz(i), nz(i + 1), x - fi);
}

// MARK: — формы волн

double sstep(double t) { return t * t * (3 - 2 * t); }
double frac(double p) { return p - std::floor(p); }
double shSine(double p) { return std::sin(tau * p); }

double shTri(double p0) {
    const double p = frac(p0);
    return 1 - 4 * std::abs(p < 0.75 ? p - 0.25 : p - 1.25);
}

double shSaw(double p0) { return 1 - 2 * frac(p0); }
double shSq(double p0) { return frac(p0) < 0.5 ? 1 : -1; }

double shape(int k, double p) {
    switch (k) {
    case 0: return shSine(p);
    case 1: return shTri(p);
    case 2: return shSaw(p);
    default: return shSq(p);
    }
}

double morph(const std::vector<int>& kinds, double idx0, double p) {
    const int n = int(kinds.size());
    const double idx = clamp(idx0, 0, double(n - 1));
    const int i = std::min(int(std::floor(idx)), n - 2);
    return lerp(shape(kinds[size_t(i)], p), shape(kinds[size_t(i + 1)], p), sstep(idx - double(i)));
}

// MARK: — сигнал и колонки

std::vector<double> gen(const std::function<double(double, int)>& fn, int cols) {
    const int n = cols * sub;
    std::vector<double> a(size_t(n + 1), 0.0);
    const double dn = double(n);
    for (int i = 0; i <= n; ++i) a[size_t(i)] = double(float(fn(double(i) / dn, i)));
    return a;
}

IconElement lane(const std::vector<double>& a, const IconLane& o, std::optional<double> hx) {
    const int n = int(a.size() - 1) / sub;
    const double w = (o.x1 - o.x0) / double(n);
    const double half = o.thin ? 0 : 0.4;
    std::vector<IconRect> rects;
    rects.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const double xc = o.x0 + (double(i) + 0.5) * w;
        double mn = 0, mx = 0;
        for (int k = 0; k <= sub; ++k) {
            const double v = a[size_t(i * sub + k)];
            if (o.mirror) {
                mx = std::max(mx, std::abs(v));
            } else {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
        }
        if (o.mirror) mn = -mx;
        double gain = 1;
        if (hx) {
            const double z = (xc - *hx) / 7;
            gain = 1 + 0.16 * std::exp(-z * z);
        }
        const double e = o.ext ? o.ext(xc) : 0;
        double top = o.y0 - (mx + e) * o.amp * gain - half;
        double bot = o.y0 - (mn - e) * o.amp * gain + half;
        if (o.thin && bot - top < 0.5) continue;
        if (top < 1.5) top = 1.5;
        if (bot > 62.5) bot = 62.5;
        rects.push_back({o.x0 + double(i) * w - 0.06, top, w + 0.12, bot - top});
    }
    IconElement el = IconElement::ofRects(std::move(rects), o.ink,
                                          o.alpha < 1 ? std::optional<double>(o.alpha) : std::nullopt);
    if (o.mirror) {
        el.contour = true;
    } else {
        // Волна генератора — сплошная фигура от оси до сигнала по всем точкам.
        const size_t m = a.size();
        std::vector<double> top(m), bot(m);
        for (size_t i = 0; i < m; ++i) {
            const double hi = std::max(0.0, a[i]);
            const double lo = std::min(0.0, a[i]);
            top[i] = std::min(62.5, std::max(1.5, o.y0 - hi * o.amp - half));
            bot[i] = std::min(62.5, std::max(1.5, o.y0 - lo * o.amp + half));
        }
        el.wave = std::move(top);
        el.waveBot = std::move(bot);
        el.waveX0 = o.x0;
        el.waveX1 = o.x1;
    }
    return el;
}

IconElement axis(double y, double xa, double xb) {
    return IconElement::ofRects({{xa, y - 0.4, xb - xa, 0.8}}, IconInk::secondary);
}

std::function<double(double)> startSpike(double k, double w) {
    return [k, w](double x) {
        const double z = (x - 4) / w;
        return k * std::exp(-z * z);
    };
}

}  // namespace EngineIcons
}  // namespace jambo
