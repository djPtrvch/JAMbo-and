// Порт SubEngineTests и FilterTypeTests.testSubLadderInCore (ветка swift JAMbo): SUB — моно-бас (OSC1 + OSC2 октавой
// ниже), голос напрямую и через ядро.
#include "engine/AudioCore.h"
#include "model/FilterType.h"
#include "model/Params.h"
#include "model/Project.h"
#include "model/SynthEngines.h"
#include "synth/Jambo.h"
#include "synth/JamboTables.h"
#include "synth/JamboVoice.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jambo;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}

const JamboTables& tables() {
    static const JamboTables t;
    return t;
}

std::vector<float> renderVoice(double note, const std::vector<double>& k, int frames = 24000) {
    JamboVoice v(tables(), 48000);
    v.trigger(J::sub, note, k[0], k[1], k[2], k[3], 500, 0, 0);
    std::vector<float> out(static_cast<size_t>(frames));
    for (int pos = 0; pos < frames;) {
        const int n = std::min(24, frames - pos);
        v.render(out.data() + pos, n, 1);
        pos += n;
    }
    return out;
}

float absMax(const std::vector<float>& x, size_t from = 0) {
    float m = 0;
    for (size_t i = from; i < x.size(); ++i) m = std::max(m, std::fabs(x[i]));
    return m;
}
bool finite(const std::vector<float>& x) {
    for (float v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

/// Посторонние (не гармоники f0) составляющие к основному тону, dBc (SynthKnobsTests.nonHarmonicDbc).
void fft(std::vector<double>& re, std::vector<double>& im) {
    const size_t n = re.size();
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / double(len);
        for (size_t s = 0; s < n; s += len)
            for (size_t k = 0; k < len / 2; ++k) {
                const double wr = std::cos(ang * double(k)), wi = std::sin(ang * double(k));
                const size_t a = s + k, b = a + len / 2;
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    }
}
double nonHarmonicDbc(const std::vector<float>& x, double f0) {
    const size_t n = x.size();
    std::vector<double> re(n), im(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const double a = 2 * M_PI * double(i) / double(n);
        re[i] = double(x[i]) * (0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a) - 0.01168 * std::cos(3 * a));
    }
    fft(re, im);
    const double binHz = 48000.0 / double(n);
    double fund = 0, worst = 0;
    for (int k = int(std::ceil(20 / binHz)); k < int(20000 / binHz); ++k) {
        const double m = std::sqrt(re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)]);
        const double h = double(k) * binHz / f0;
        const bool near = std::fabs(h - std::round(h)) * f0 / binHz <= 6 && std::round(h) >= 1;
        if (near && std::round(h) == 1) fund = std::max(fund, m);
        else if (!near) worst = std::max(worst, m);
    }
    return 20 * std::log10(std::max(worst, 1e-12) / std::max(fund, 1e-12));
}

void table() {
    const auto* e = SynthEngines::engine(24);
    const auto& g = Params::grid("mi:24");
    Track t(0);
    t.setSynth(24);
    check(e && e->name == "SUB" && !e->decay && J::mono(J::sub) && g[0].steps == 3 &&
              g[0].format(100.0 / 6) == "Triangle" && g[0].format(90) == "Square" && g[1].label == "Sub" &&
              g[2].label == "Fat" && g[3].label == "Grit" && t.params.at("timbre") == 40 && t.params.at("morph") == 0 &&
              t.params.at("p4") == 0,
          "sub: таблица движка и значения при выборе");
}

void voice() {
    float worst = 0;
    bool ok = true;
    for (double note : {12.0, 36.0, 60.0, 84.0})
        for (double w : {0.1, 0.5, 0.9})
            for (double a : {0.0, 0.5, 1.0}) {
                const auto x = renderVoice(note, {w, a, 1 - a, a}, 12000);
                ok = ok && finite(x);
                worst = std::max(worst, absMax(x));
            }
    std::printf("SUB peak: %.1f dBFS\n", 20 * std::log10(worst));
    check(ok && worst <= 1, "sub: без NaN, пик ≤ 0 dBFS");
    const auto a = renderVoice(36, {0.5, 0.4, 0, 0}, 4800), b = renderVoice(36, {0.5, 0.4, 0, 0}, 4800);
    check(a == b && std::fabs(a[0]) < 0.01f, "sub: Key Reset, плавный вход");
    {
        JamboVoice v(tables(), 48000);
        v.trigger(J::sub, 36, 0.5, 0.4, 0, 0, 500, 0, 0);
        std::vector<float> out(12000);
        for (int pos = 0; pos < 12000;) {
            if (pos == 6000) v.trigger(J::sub, 43, 0.5, 0.4, 0, 0, 500, 0, 0);
            const int n = std::min(24, 12000 - pos);
            v.render(out.data() + pos, n, 1);
            pos += n;
        }
        float steady = 0, seam = 0;
        for (int i = 2000; i < 6000; ++i) steady = std::max(steady, std::fabs(out[size_t(i)] - out[size_t(i - 1)]));
        for (int i = 9000; i < 12000; ++i) steady = std::max(steady, std::fabs(out[size_t(i)] - out[size_t(i - 1)]));
        for (int i = 6000; i < 6200; ++i) seam = std::max(seam, std::fabs(out[size_t(i)] - out[size_t(i - 1)]));
        check(seam <= steady * 1.2f + 1e-4f, "sub: новая нота — фазы продолжаются");
    }
    const auto mag = [](const std::vector<float>& x, double f) {
        double re = 0, im = 0;
        for (size_t i = 4800; i < x.size(); ++i) {
            const double a = 2 * M_PI * f * double(i) / 48000;
            re += double(x[i]) * std::cos(a);
            im += double(x[i]) * std::sin(a);
        }
        return std::sqrt(re * re + im * im) / double(x.size() - 4800);
    };
    const auto off = renderVoice(45, {0.1, 0, 0, 0}, 48000), on = renderVoice(45, {0.1, 1, 0, 0}, 48000);
    check(mag(off, 55) < mag(off, 110) * 0.01 && mag(on, 55) > mag(on, 110) * 0.5 && mag(on, 110) > mag(off, 110) * 0.3,
          "sub: SUB подмешивает октаву ниже");
    const auto clean = renderVoice(45, {0.1, 0, 0, 0}), dirty = renderVoice(45, {0.1, 0, 0, 1});
    const auto rms = [](const std::vector<float>& x) {
        double s = 0;
        for (size_t i = 4800; i < x.size(); ++i) s += double(x[i]) * x[i];
        return float(std::sqrt(s / double(x.size() - 4800)));
    };
    check(rms(dirty) > rms(clean) * 1.2f && absMax(dirty) <= 1, "sub: GRIT плотнее при том же пике");
    const auto saw = renderVoice(84, {0.5, 0, 0, 0}, 4800 + 16384);
    const double db = nonHarmonicDbc(std::vector<float>(saw.begin() + 4800, saw.end()), 440 * std::pow(2, 15 / 12.0));
    std::printf("SUB saw C6 aliasing: %.1f dBc\n", db);
    check(db < -60, "sub: алиасинг пилы C6 ниже −60 dBc");
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 8; ++k) renderVoice(36 + k, {0.5, 0.4, 0.5, 0.5}, 48000);
    std::printf("SUB: 8 voices — %.1f%% of one core\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 100);
}

std::vector<float> renderCore(AudioCore& c, int blocks) {
    std::vector<float> l(256), r(256), y;
    for (int i = 0; i < blocks; ++i) {
        c.render(256, l.data(), r.data());
        while (c.sequencer.out.pop()) {}
        y.insert(y.end(), l.begin(), l.end());
    }
    return y;
}

void core() {
    {
        AudioCore c(false);
        Project p;
        p.tracks[0].setSynth(24);
        p.tracks[0].vol = 0;
        c.installSynth();
        c.publish(p);
        c.events.push(CoreEvent::noteOn(0, 36, 1, 1));
        auto y = renderCore(c, 40);
        c.events.push(CoreEvent::noteOn(0, 43, 2, 1));
        const auto z = renderCore(c, 60);
        y.insert(y.end(), z.begin(), z.end());
        float steady = 0, seam = 0;
        for (int i = 4000; i < 10240; ++i) steady = std::max(steady, std::fabs(y[size_t(i)] - y[size_t(i - 1)]));
        for (int i = 16000; i < 25000; ++i) steady = std::max(steady, std::fabs(y[size_t(i)] - y[size_t(i - 1)]));
        for (int i = 10240; i < 10900; ++i) seam = std::max(seam, std::fabs(y[size_t(i)] - y[size_t(i - 1)]));
        check(seam <= steady * 1.5f + 1e-4f && c.sampler.synth->activeCount() == 1, "core sub: легато без щелчка, один голос");
    }
    {
        AudioCore c(false);
        Project p;
        p.bpm = 126;
        auto& t = p.tracks[0];
        t.setSynth(24);
        t.vol = 0;
        t.params["timbre"] = 36;
        t.params["morph"] = 43;
        t.params["attack"] = 0;
        t.params["decay"] = 500;
        for (int s : {4, 7, 10, 14}) {
            t.pagesMut()[0][size_t(s)].on = true;
            t.pagesMut()[0][size_t(s)].notes = {48};
        }
        c.installSynth();
        c.publish(p);
        c.events.push(CoreEvent::tempo(126));
        c.events.push(CoreEvent::play(true));
        const auto y = renderCore(c, 720);
        std::vector<float> hf(y.size(), 0.f);
        for (size_t i = 2; i < y.size(); ++i) hf[i] = std::fabs(y[i] - 2 * y[i - 1] + y[i - 2]);
        const int step = int(std::lround(48000 * 60 / 126.0 / 4));
        const auto maxIn = [&](int a, int b) {
            float m = 0;
            for (int i = a; i < b; ++i) m = std::max(m, hf[size_t(i)]);
            return m;
        };
        const float first = maxIn(4 * step - 100, 4 * step + 400);
        bool ok = true;
        for (int s : {7, 10, 14, 20, 23, 26, 30}) ok = ok && maxIn(s * step - 500, s * step + 400) <= first * 1.2f;
        check(ok, "core sub: ноты секвенсора — стык без обрыва");
    }
}

std::vector<float> ladderRender(int engine, int type, double grit = 0) {
    Project p;
    for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0].reset();
    auto& tr = p.tracks[0];
    tr.setSynth(engine);
    tr.params[Params::filterTypeId] = double(type);
    tr.params["filter"] = 55;
    tr.params["reso"] = 60;
    tr.params["p4"] = grit;
    tr.params["timbre"] = 0;
    auto& s = tr.clips[0]->pages[0][0];
    s.on = true;
    s.notes = {36};
    s.len = 8;
    AudioCore c(false);
    c.installSynth();
    c.publish(p);
    c.events.push(CoreEvent::play(true));
    auto y = renderCore(c, (24000 + 255) / 256);
    return y;
}

void ladder() {
    const auto lp = ladderRender(J::sub, FilterType::lp);
    const auto lad = ladderRender(J::sub, FilterType::ladder);
    const auto hot = ladderRender(J::sub, FilterType::ladder, 100);
    bool ok = true;
    for (const auto* y : {&lp, &lad, &hot}) ok = ok && finite(*y) && absMax(*y) <= 1 && absMax(*y) > 0.01f;
    float d1 = 0, d2 = 0;
    for (size_t i = 0; i < lp.size(); ++i) d1 = std::max(d1, std::fabs(lp[i] - lad[i]));
    for (size_t i = 0; i < lad.size(); ++i) d2 = std::max(d2, std::fabs(hot[i] - lad[i]));
    check(ok && d1 > 0.01f && d2 > 0.01f, "core sub: LADDER — свой фильтр, GRIT перегружает вход");
    check(ladderRender(17, FilterType::ladder) == ladderRender(17, FilterType::lp), "core: у ANALOG LADDER — как LP");
}
}  // namespace

int runSubCoreTests() {
    table();
    voice();
    core();
    ladder();
    std::printf("sub core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
