// Мастер: порт MasterTests (без AudioCore), MasterAutoMakeupTests, MasterUnityGuardTests (ветка swift JAMbo).
#include "master/Glue.h"
#include "master/Limiter.h"
#include "master/MasterChain.h"
#include "model/Params.h"
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
bool near(double a, double b, double e) { return std::fabs(a - b) <= e; }
using I = MasterChain::Index;
constexpr int N = 256;

void fill(std::vector<float>& l, std::vector<float>& r, int b, double amp, int n = N) {
    for (int i = 0; i < n; ++i) {
        const float v = float(amp * std::sin(2 * M_PI * 1000 * double(b * n + i) / 48000));
        l[size_t(i)] = v;
        r[size_t(i)] = v;
    }
}
double peak(const std::vector<float>& x) {
    float m = 0;
    for (float v : x) m = std::max(m, std::fabs(v));
    return double(m);
}

void limiterCeiling() {
    const int n = 128;
    MasterLimiter lim(48000);
    std::vector<float> l(n), r(n);
    float pk = 0;
    for (int b = 0; b < 375; ++b) {
        fill(l, r, b, 1, n);
        lim.process(l.data(), r.data(), l.data(), r.data(), n, 6, -1, 3, 150, false, 0);
        if (b > 10)
            for (float v : l) pk = std::max(pk, std::fabs(v));
    }
    check(20 * std::log10(double(pk)) <= -1.0 + 1e-3 && near(lim.frameGr, 7, 0.3), "limiter: потолок −1 dB, GR ~7 dB");
}

void chainSilence() {
    MasterChain chain(48000);
    std::vector<float> l(N), r(N);
    double p[I::count];
    for (int k = 0; k < I::count; ++k) p[k] = MasterChain::defaults[k];
    for (int b = 0; b < 200; ++b) {
        std::fill(l.begin(), l.end(), 0.f);
        std::fill(r.begin(), r.end(), 0.f);
        chain.process(l.data(), r.data(), N, p);
    }
    check(peak(l) < 1e-12, "chain: тишина остаётся тишиной");
}

void autoMakeup() {
    const int n = 256 * 188;
    std::vector<float> src(static_cast<size_t>(n)), out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) src[size_t(i)] = float(0.5 * std::sin(2 * M_PI * 1000 * double(i) / 48000));
    auto run = [&](MasterGlue& g, double bypass) {
        for (int i = 0; i < n; i += 256)
            g.process(src.data() + i, src.data() + i, out.data() + i, out.data() + i, 256, -20, 10, 100, false, 0, 4,
                      0, 100, 20, bypass);
    };
    MasterGlue g(48000);
    run(g, 0);
    const bool none = g.kCount == 0;
    g.measuring = true;
    g.resetMeasure();
    run(g, 0);
    check(none && near(10 * std::log10(g.kSumIn / g.kSumOut), 10.5, 0.7), "auto makeup: замер сжатия");
    MasterGlue b(48000);
    b.measuring = true;
    run(b, 1);
    check(near(10 * std::log10(b.kSumIn / b.kSumOut), 0, 0.01), "auto makeup: BYPASS");
}

struct Heard {
    double meter, heard;
};
template <typename F>
Heard runChain(F set, long unity) {
    MasterChain chain(48000);
    chain.unity.store(unity);
    std::vector<float> l(N), r(N);
    double p[I::count];
    for (int k = 0; k < I::count; ++k) p[k] = MasterChain::defaults[k];
    set(p);
    double meter = 0, heard = 0;
    for (int k = 0; k < 400; ++k) {
        fill(l, r, k, 0.5);
        chain.process(l.data(), r.data(), N, p);
        if (k > 100) meter = std::max(meter, peak(l));
        chain.applyUnity(l.data(), r.data(), N);
        if (k > 100) heard = std::max(heard, peak(l));
    }
    return {meter, heard};
}

void unity() {
    const Heard lim = runChain([](double* p) { p[I::limGain] = 12; }, MasterScope::lim + 1);
    const Heard clip = runChain([](double* p) { p[I::clipDrive] = 6; }, MasterScope::clip + 1);
    const Heard off = runChain([](double* p) { p[I::limGain] = 12; }, 0);
    const Heard byp = runChain(
        [](double* p) {
            p[I::limGain] = 12;
            p[I::limBypass] = 1;
        },
        MasterScope::lim + 1);
    check(near(20 * std::log10(lim.heard / lim.meter), -12, 0.01), "unity: LIM −INPUT после метров");
    check(near(20 * std::log10(clip.heard / clip.meter), -6, 0.01), "unity: CLIP −INPUT");
    check(off.heard == off.meter && near(lim.meter, off.meter, 1e-9), "unity: выключен — без изменений");
    check(byp.heard == byp.meter, "unity: BYPASS — 0 dB");
}

void guard() {
    MasterLimiter lim(48000), off(48000);
    std::vector<float> l(N), r(N);
    const int perSec = 48000 / N;
    double grSum = 0;
    int grN = 0;
    for (int k = 0; k < 25 * perSec; ++k) {
        fill(l, r, k, 1);
        lim.process(l.data(), r.data(), l.data(), r.data(), N, 12, -1, 3, 150, true, 0, 3);
        fill(l, r, k, 1);
        off.process(l.data(), r.data(), l.data(), r.data(), N, 12, -1, 3, 150, true, 0);
        if (k > 20 * perSec) {
            grSum += lim.frameGr;
            ++grN;
        }
    }
    check(off.guardCut == 0 && off.frameGr > 12, "guard: OFF — вход не трогается");
    check(lim.guardCut > 8 && lim.guardCut <= 12 && near(grSum / grN, 3, 1.5), "guard: GR приходит к пределу");
    for (int k = 0; k < 60 * perSec; ++k) {
        fill(l, r, k, 0.063);
        lim.process(l.data(), r.data(), l.data(), r.data(), N, 12, -1, 3, 150, true, 0, 3);
    }
    check(lim.guardCut == 0, "guard: тихо — снижение возвращается");
    const ParamDef* g = Params::byId("lim.guard");
    check(g && g->format(0) == "OFF" && g->format(4) == "4.0 dB" && g->fromNorm(0.02) == 0 &&
              MasterChain::defaultValues()["lim.guard"] == 0,
          "guard: параметр проекта");
}
}  // namespace

int runMasterTests() {
    limiterCeiling();
    chainSilence();
    autoMakeup();
    unity();
    guard();
    std::printf("master: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
