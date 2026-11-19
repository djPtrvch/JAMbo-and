// TEXTURE и GRANULAR — проверка по свойствам. TEXTURE — как TextureEngineTests.swift JAMbo (уровень ±35 % по сетке
// ручек и нот, ровность окон 10 мс); GRANULAR — базовая (нет NaN, слышно, лимит зёрен, тишина без сэмпла).
// Эталонов у этих движков нет.
#include "synth/TextureVoice.h"
#include "synth/GranularVoice.h"
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jambo;

namespace {

int fails = 0;
void check(bool ok, const std::string& what) {
    std::printf("%-58s %s\n", what.c_str(), ok ? "OK" : "FAIL");
    if (!ok) ++fails;
}

double rms(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0;
    for (size_t i = a; i < b; ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(1, b - a)));
}

void renderTexture(const double k[4], double note, int frames, std::vector<float>& l, std::vector<float>& r) {
    TextureVoice v(48000, 1);
    v.silence();
    v.trigger(note, k[0], k[1], k[2], k[3]);
    l.assign(frames, 0);
    r.assign(frames, 0);
    for (int pos = 0; pos < frames; pos += 24) {
        const int n = std::min(24, frames - pos);
        v.render(l.data() + pos, r.data() + pos, n, INT_MAX);
    }
}

double textureLevel(const double k[4], double note) {
    std::vector<float> l, r;
    renderTexture(k, note, 48000, l, r);
    return (rms(l, 9600, l.size()) + rms(r, 9600, r.size())) / 2;
}

double cv(const std::vector<float>& x, size_t from) {
    std::vector<double> w;
    for (size_t i = from; i + 480 <= x.size(); i += 480) w.push_back(rms(x, i, i + 480));
    double m = 0;
    for (double v : w) m += v;
    m /= double(w.size());
    double sd = 0;
    for (double v : w) sd += (v - m) * (v - m);
    return std::sqrt(sd / double(w.size())) / m;
}

}  // namespace

int runTexGran() {
    fails = 0;
    // TEXTURE: уровень при любых ручках — ±35 % от уровня по умолчанию.
    const double def[4] = {0.4, 0.3, 0.3, 0};
    const double ref = textureLevel(def, 57);
    double worst = 0;
    bool finite = true;
    for (double c : {0.0, 0.4, 1.0}) for (double g : {0.0, 0.3, 1.0}) for (double t : {0.0, 0.3, 1.0})
        for (double m : {0.0, 1.0}) for (double note : {45.0, 57.0, 72.0}) {
            const double k[4] = {c, g, t, m};
            const double x = textureLevel(k, note) / ref;
            if (!std::isfinite(x)) finite = false;
            worst = std::max(worst, std::fabs(x - 1));
        }
    char buf[96];
    std::snprintf(buf, sizeof buf, "TEXTURE: level spread +-%.0f%% (<= 35%%), RMS %.3f", worst * 100, ref);
    check(finite && worst <= 0.35, buf);
    // Ровность: CV окон 10 мс при Grit 0 ≤ 0,1, при Grit 1 ≤ 0,2.
    for (auto [g, lim] : {std::pair<double, double>{0.0, 0.1}, {1.0, 0.2}}) {
        const double k[4] = {0.4, g, 0.3, 0};
        std::vector<float> l, r;
        renderTexture(k, 57, 96000, l, r);
        const double c = cv(l, 9600);
        std::snprintf(buf, sizeof buf, "TEXTURE: Grit %.0f evenness CV %.3f (<= %.1f)", g, c, lim);
        check(c <= lim, buf);
    }

    // GRANULAR: источник — пила 110 Гц, 4 с, моно; период известен.
    const int srcN = 48000 * 4;
    std::vector<float> src(srcN);
    for (int i = 0; i < srcN; ++i) {
        const double ph = std::fmod(110.0 * i / 48000, 1.0);
        src[i] = float(0.8 * (1 - 2 * ph));
    }
    GrainSrc gs;
    gs.left = src.data();
    gs.frames = srcN;
    gs.period = 1.0 / 110;
    bool gFinite = true;
    float gPeak = 0;
    int maxGrains = 0;
    for (double size : {0.0, 0.62, 1.0}) for (double dens : {0.0, 0.6, 1.0}) for (double spray : {0.0, 0.25, 1.0}) {
        GranularVoice v(48000, 1);
        v.trigger(48, 0.3, size, dens, spray);
        std::vector<float> l(48000), r(48000);
        for (int pos = 0; pos < 48000; pos += 24) {
            v.render(l.data() + pos, r.data() + pos, 24, INT_MAX, gs);
            maxGrains = std::max(maxGrains, v.grains());
        }
        for (size_t i = 0; i < l.size(); ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) gFinite = false;
            gPeak = std::max(gPeak, std::max(std::fabs(l[i]), std::fabs(r[i])));
        }
    }
    std::snprintf(buf, sizeof buf, "GRANULAR: grid, no NaN, peak %.2f, grains <= %d", gPeak, maxGrains);
    check(gFinite && gPeak > 0.05f && gPeak < 4 && maxGrains <= GranularVoice::maxGrains, buf);
    {
        GranularVoice v(48000, 1);
        v.trigger(48, 0.3, 0.62, 0.6, 0.25);
        std::vector<float> l(24), r(24);
        bool silent = true;
        for (int pos = 0; pos < 9600; pos += 24) {
            v.render(l.data(), r.data(), 24, INT_MAX, GrainSrc());
            for (int i = 0; i < 24; ++i) if (l[i] != 0 || r[i] != 0) silent = false;
        }
        check(silent, "GRANULAR: silence without sample");
    }
    return fails;
}
