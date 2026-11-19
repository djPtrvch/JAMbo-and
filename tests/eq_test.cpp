// EQ, спектры, уровни: порт EqTests, EqDynTests, EqListenTests, SpectrumTests, LevelsTests (ветка swift JAMbo) — без
// AudioCore: EqUnit и EqListen напрямую, EQ в сэмплере — Sampler с TrackEqs, FxRack и Levels.
#include "engine/Levels.h"
#include "eq/EqCoeffs.h"
#include "eq/EqDyn.h"
#include "eq/EqListen.h"
#include "eq/EqSnapshot.h"
#include "eq/EqUnit.h"
#include "eq/Spectrum.h"
#include "eq/TrackEqs.h"
#include "fx/FxRandom.h"
#include "model/Project.h"
#include "sampler/Sampler.h"
#include "sampler/SoundSnapshot.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
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
EqBand band(int type, double f, double g, double q, int slope = 0) { return EqBand(true, type, f, g, q, slope); }

void response() {
    check(near(EqCoeffs::response(band(EqSpec::bell, 1000, 6, 1), 1000), 6, 1e-9), "eq: АЧХ bell");
    check(EqCoeffs::response(band(EqSpec::notch, 1000, 0, 4), 1000) < -100, "eq: АЧХ notch");
    check(near(EqCoeffs::response(band(EqSpec::highShelf, 6000, -8, 1), 20000), -8, 0.3), "eq: АЧХ high shelf");
    const double one = EqCoeffs::response(band(EqSpec::lowCut, 1000, 0, 1, 0), 200);
    const double four = EqCoeffs::response(band(EqSpec::lowCut, 1000, 0, 1, 2), 200);
    check(near(four, 4 * one, 1e-9), "eq: каскад 48 dB/окт");
}

// EqListenTests.
struct Lv {
    double l, r;
};
Lv listenLevel(double hz, const EqBand& b, bool on, int blocks = 40) {
    const int n = 256;
    EqUnit u(48000);
    EqListen x(48000);
    std::vector<double> p(EqSpec::stride);
    const Eq empty;
    EqSpec::fill(p.data(), &empty);
    const auto f = EqListen::filter(b);
    x.kind.store(f.kind);
    x.q.store(float(f.q));
    x.freq.store(float(b.freq));
    x.ms.store(b.ms);
    x.channel.store(on ? 0 : -1);
    std::vector<float> l(n), r(n);
    double sl = 0, sr = 0;
    int k = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        for (int i = 0; i < n; ++i) {
            const double ph = 2 * M_PI * hz * double(k) / 48000;
            l[size_t(i)] = float(0.5 * std::sin(ph));
            r[size_t(i)] = float(0.25 * std::sin(ph));
            ++k;
        }
        u.process(l.data(), r.data(), n, p.data());
        x.process(0, u, n);
        if (blk >= blocks / 2)
            for (int i = 0; i < n; ++i) {
                sl += double(l[size_t(i)]) * l[size_t(i)];
                sr += double(r[size_t(i)]) * r[size_t(i)];
            }
    }
    const double cnt = double(n * (blocks - blocks / 2));
    return {10 * std::log10(sl / cnt + 1e-30), 10 * std::log10(sr / cnt + 1e-30)};
}

void listen() {
    const EqBand b = band(EqSpec::bell, 1000, 6, 2);
    const Lv dry = listenLevel(1000, b, false), at = listenLevel(1000, b, true), away = listenLevel(100, b, true);
    check(near(at.l, dry.l, 0.2) && away.l < dry.l - 25, "listen: bell");
    const EqBand low = band(EqSpec::lowCut, 500, 0, 0.71);
    check(listenLevel(5000, low, true).l < listenLevel(5000, low, false).l - 30 &&
              near(listenLevel(50, low, true).l, listenLevel(50, low, false).l, 0.3),
          "listen: low cut — ФНЧ");
    const EqBand high = band(EqSpec::highShelf, 5000, 3, 0.71);
    check(listenLevel(300, high, true).l < listenLevel(300, high, false).l - 30, "listen: high shelf — ФВЧ");
    EqBand side = band(EqSpec::bell, 1000, 0, 1);
    side.ms = 2;
    const Lv s = listenLevel(1000, side, true);
    check(near(s.l, -21.07, 0.3) && near(s.r, -21.07, 0.3), "listen: SIDE");
    check(near(listenLevel(100, band(EqSpec::bell, 1000, 0, 1), false).l, -9.03, 0.1), "listen: выключено");
    EqUnit u(48000);
    EqListen x(48000);
    std::vector<float> l(256, 0.1f), r(256, 0.1f);
    u.laneL[0] = l.data();
    u.laneR[0] = r.data();
    x.channel.store(3);
    x.process(3, u, 256);
    bool ok = x.active == 3;
    x.process(5, u, 256);
    ok = ok && x.active == 3;
    x.channel.store(5);
    x.process(3, u, 256);
    x.process(3, u, 256);
    ok = ok && x.active == -1;
    x.process(5, u, 256);
    check(ok && x.active == 5, "listen: канал отпускается и берётся следующий");
    const auto sp = EqListen::span(band(EqSpec::bell, 1000, 0, 2));
    check(near(sp.hi - sp.lo, 500, 1e-9) && near(std::sqrt(sp.lo * sp.hi), 1000, 1e-9), "listen: область bell");
    const auto lw = EqListen::span(band(EqSpec::lowCut, 200, 0, 0.71));
    check(lw.lo == 0 && lw.hi == 200 && std::isinf(EqListen::span(band(EqSpec::highShelf, 5000, 0, 0.71)).hi),
          "listen: область cut и shelf");
}

void spectrum() {
    const int n = 256;
    FxRandom rnd(9);
    std::vector<float> x(static_cast<size_t>(n));
    for (auto& v : x) v = float(rnd.next() * 2 - 1);
    RealFFT fft(n);
    std::vector<float> re(n / 2), im(n / 2);
    fft.forward(x.data(), re.data(), im.data());
    bool ok = true;
    for (int k = 0; k < n / 2; ++k) {
        double sr = 0, si = 0;
        for (int i = 0; i < n; ++i) {
            const double a = -2 * M_PI * double(k * i) / double(n);
            sr += double(x[size_t(i)]) * std::cos(a);
            si += double(x[size_t(i)]) * std::sin(a);
        }
        ok = ok && near(re[size_t(k)], sr, 1e-3) && near(im[size_t(k)], si, 1e-3);
    }
    check(ok, "spectrum: БПФ = ДПФ");
    SpectrumAnalyzer a;
    const int bin = 100;
    const double f = double(bin) * 48000 / 4096;
    std::vector<float> s(4096);
    for (int i = 0; i < 4096; ++i) s[size_t(i)] = float(std::sin(2 * M_PI * f * double(i) / 48000));
    for (int i = 0; i < 40; ++i) a.analyze(s);
    check(near(a.db[bin], 20 * std::log10(0.21), 0.01) && a.db[bin + 10] < -100, "spectrum: синус в своём бине");
}

// LevelsTests (громкость).
double kGain(double hz) {
    const double c[10] = {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585,
                          1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    const double w = 2 * M_PI * hz / 48000;
    double g = 1;
    for (int s = 0; s < 2; ++s) {
        const double* k = c + s * 5;
        const double re1 = k[0] + k[1] * std::cos(w) + k[2] * std::cos(2 * w);
        const double im1 = -k[1] * std::sin(w) - k[2] * std::sin(2 * w);
        const double re2 = 1 + k[3] * std::cos(w) + k[4] * std::cos(2 * w);
        const double im2 = -k[3] * std::sin(w) - k[4] * std::sin(2 * w);
        g *= std::sqrt((re1 * re1 + im1 * im1) / (re2 * re2 + im2 * im2));
    }
    return g;
}
void feed(Loudness& m, double seconds, float amp, double hz, double& phase) {
    const int n = 256;
    std::vector<float> l(n), r(n, 0.f);
    for (int b = 0; b < int(seconds * 48000 / n); ++b) {
        for (int i = 0; i < n; ++i) {
            l[size_t(i)] = amp * float(std::sin(phase));
            phase += 2 * M_PI * hz / 48000;
        }
        (void)m.listens(0);
        m.process(l.data(), r.data(), n);
        m.end(n);
    }
}

void loudness() {
    for (double db : {-23.0, -33.0}) {
        const float amp = float(std::pow(10, db / 20));
        const int n = 48000 * 20;
        std::vector<float> s(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) s[size_t(i)] = amp * float(std::sin(2 * M_PI * 1000 * double(i) / 48000));
        Loudness m(48000);
        m.source.store(0);
        for (int at = 0; at < n; at += 256) {
            (void)m.listens(0);
            m.process(s.data() + at, s.data() + at, 256);
            m.end(256);
        }
        const double file = Loudness::integratedOf(s.data(), s.data(), n);
        check(near(m.momentary.load(), db, 0.1) && near(m.shortTerm.load(), db, 0.1) &&
                  near(m.integrated.load(), db, 0.1) && near(file, db, 0.1),
              "loudness: EBU 3341 " + std::to_string(int(db)));
    }
    Loudness m(48000);
    m.source.store(0);
    double ph = 0;
    const double k = kGain(997);
    const double full = -0.691 + 10 * std::log10(0.5 * k * k);
    feed(m, 10, 1, 997, ph);
    check(near(m.momentary.load(), full, 0.05) && near(m.integrated.load(), full, 0.1) && near(full, -3, 0.3),
          "loudness: синус 997 Гц");
    const double before = m.integrated.load();
    feed(m, 10, 0, 997, ph);
    check(std::isinf(m.momentary.load()) && near(m.integrated.load(), before, 0.15), "loudness: тишина не считается");
    const double mid = m.integrated.load();
    feed(m, 10, 0.01f, 997, ph);
    check(near(m.integrated.load(), mid, 0.01), "loudness: относительный порог");
    m.resetRequests.store(1);
    feed(m, 10, 0.1f, 997, ph);
    check(near(m.integrated.load(), full - 20, 0.1), "loudness: сброс");
    m.source.store(1);
    (void)m.listens(0);
    check(std::isinf(m.integrated.load()), "loudness: смена канала — сброс");
}

// EQ в сэмплере.
using Samples = std::map<std::string, SampleRef>;
struct Rig {
    Sampler s{48000};
    TrackEqs eqs{48000};
    FxRack fx{48000};
    Levels levels;
    Spectra spectra;
    EqDynLevels eqDyn;
    std::vector<std::unique_ptr<SoundSnapshot>> snaps;
    std::vector<uint32_t> lockMask = std::vector<uint32_t>(SoundData::maxTracks, 0);
    std::vector<float> lockVal = std::vector<float>(size_t(SoundData::maxTracks * Params::fxCount), 0.f);
    int64_t frame = 0;
    Rig() {
        s.eqs = &eqs;
        s.fx = &fx;
        s.levels = &levels;
        s.spectra = &spectra;
        s.eqDyn = &eqDyn;
    }
    void publish(const Project& p, const Samples& smp) {
        snaps.push_back(std::make_unique<SoundSnapshot>(p, smp, SoundSnapshot::Options()));
        s.adopt(snaps.back()->mutableData(), frame);
    }
    std::vector<float> run(int frames) {
        std::vector<float> out;
        std::vector<float> bl(256), br(256);
        while (int(out.size()) < frames) {
            std::fill(bl.begin(), bl.end(), 0.f);
            std::fill(br.begin(), br.end(), 0.f);
            fx.begin(*s.sound(), lockMask.data(), lockVal.data());
            levels.begin();
            s.render(frame, 256, bl.data(), br.data());
            levels.commit(256);
            out.insert(out.end(), bl.begin(), bl.end());
            frame += 256;
        }
        return out;
    }
};
double rms(const std::vector<float>& x, int a, int b) {
    double s = 0;
    for (int i = a; i < b; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    return std::sqrt(s / std::max(1, b - a));
}
SampleRef sine100() {
    std::vector<float> x(48000);
    for (int i = 0; i < 48000; ++i) x[size_t(i)] = float(0.5 * std::sin(2 * M_PI * 100 * double(i) / 48000));
    return std::make_shared<SampleData>(x, nullptr);
}
std::vector<float> runSine(const std::map<std::string, double>& sends, bool cut, bool bypass = false) {
    Project p;
    p.tracks[0].sample = "s";
    p.tracks[0].vol = 0;
    for (const auto& [k, v] : sends) p.tracks[0].params[k] = v;
    if (cut) p.tracks[0].eq.bands[0] = band(EqSpec::lowCut, 4000, 0, 0.7, 2);
    if (bypass) p.tracks[0].eq.on = false;
    Rig g;
    g.publish(p, {{"s", sine100()}});
    g.run(4096);
    g.s.noteOn(0, 48, 1, 1, g.frame);
    return g.run(36000);
}

void trackEqDryAndSends() {
    const double dry = rms(runSine({}, false), 12000, 24000);
    const double cut = rms(runSine({}, true), 12000, 24000);
    check(dry > 0.1 && 20 * std::log10(cut / dry) < -80, "eq дорожки: сухой сигнал");
    const double echo = rms(runSine({{"delay", 100}}, false), 13000, 23000);
    const double echoCut = rms(runSine({{"delay", 100}}, true), 13000, 23000);
    check(echo > dry * 1.2 && 20 * std::log10(echoCut / echo) < -80, "eq дорожки: посыл Delay");
    check(near(rms(runSine({}, true, true), 12000, 24000), dry, 1e-6), "eq дорожки: BYPASS");
}

void ringingTail() {
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].vol = 0;
    p.tracks[0].params["decay"] = 0;
    p.tracks[0].eq.bands[0] = band(EqSpec::bell, 200, 18, 18);
    std::vector<float> x(480);
    for (int i = 0; i < 480; ++i) x[size_t(i)] = float(std::sin(2 * M_PI * 200 * double(i) / 48000));
    Rig g;
    g.publish(p, {{"k", std::make_shared<SampleData>(x, nullptr)}});
    g.run(4096);
    g.s.noteOn(0, 48, 1, 1, g.frame);
    const auto o = g.run(144000);
    int last = 0;
    for (int i = 0; i < int(o.size()); ++i)
        if (std::fabs(o[size_t(i)]) > 1e-12f) last = i;
    check(rms(o, 9600, 14400) > 1e-3 && !g.eqs.ringing(0) && last > 48000 && last < 120000,
          "eq дорожки: хвост звучит после голоса и затихает");
}

struct Duck {
    double rms;
    float off;
};
Duck duck(bool source, int listener = 0) {
    Project p;
    p.tracks[size_t(listener)].sample = "s";
    p.tracks[size_t(listener)].vol = 0;
    p.tracks[1].sample = "k";
    p.tracks[1].mute = true;
    EqBand b = band(EqSpec::bell, 100, 0, 1);
    b.dyn = 2;
    b.thr = -40;
    b.range = -12;
    p.tracks[size_t(listener)].eq.bands[0] = b;
    Rig g;
    g.publish(p, {{"s", sine100()}, {"k", sine100()}});
    g.run(4096);
    g.s.noteOn(listener, 48, 1, 1, g.frame);
    if (source) g.s.noteOn(1, 48, 2, 1, g.frame);
    const auto o = g.run(24000);
    return {rms(o, 12000, 24000), g.eqDyn.offset(listener, 0)};
}

void dynTrackSource() {
    const Duck d0 = duck(false), d1 = duck(true), d2 = duck(true, 2);
    check(d0.rms > 0.1 && d0.off == 0, "eq dyn: без источника — без сдвига");
    check(near(d1.off, -12, 0.01) && near(20 * std::log10(d1.rms / d0.rms), -12, 0.3), "eq dyn: источник T2");
    check(near(20 * std::log10(d2.rms / d0.rms), -12, 0.3), "eq dyn: слушатель раньше источника");
}

void levelsPeakBeforeMute() {
    for (bool mute : {false, true}) {
        Project p;
        p.tracks[0].sample = "dc";
        p.tracks[0].vol = -6;
        p.tracks[0].mute = mute;
        Rig g;
        g.publish(p, {{"dc", std::make_shared<SampleData>(std::vector<float>(96000, 0.5f), nullptr)}});
        g.s.noteOn(0, 48, 1, 1, 0);
        g.run(9600);
        float l = 0, r = 0, o = 0;
        g.levels.read(0, [&](const float* b) {
            l = std::max(l, Levels::value(b, 0, 0));
            r = std::max(r, Levels::value(b, 0, 1));
            o = std::max(o, Levels::value(b, 1, 0));
        });
        check(l > 0.1f && near(l, r, 1e-4) && o == 0, std::string("levels: пик дорожки до mute ") + (mute ? "M" : ""));
    }
}

void tapsInCallback() {
    Project p;
    p.tracks[0].sample = "s";
    p.tracks[0].eq.bands[0] = band(EqSpec::lowCut, 4000, 0, 0.7, 2);
    Rig g;
    g.publish(p, {{"s", sine100()}});
    g.spectra.channel.store(0);
    g.run(16 * 256);
    g.s.noteOn(0, 48, 1, 1, g.frame);
    g.run(40 * 256);
    auto peak = [](const SpectrumTap& t) {
        SpectrumAnalyzer a;
        a.analyze(t);
        float m = -240;
        for (float v : a.db) m = std::max(m, v);
        return m;
    };
    check(peak(g.spectra.pre) > -40 && peak(g.spectra.post) < -100, "spectra: съём до и после EQ");
}
}  // namespace

int runEqTests() {
    response();
    listen();
    spectrum();
    loudness();
    trackEqDryAndSends();
    ringingTail();
    dynTrackSource();
    levelsPeakBeforeMute();
    tapsInCallback();
    std::printf("eq: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
