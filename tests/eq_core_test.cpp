// EQ через ядро и правки EQ проекта: порт EqRoutingTests (testEdits, testFill, testTrackEqOnDryAndSends, testMasterEq,
// testRingingTail), EqTests.testTapsInCallback и EqDynTests (testEdits, testFill, testRemap, testMixOrder,
// testTrackSource) ветки swift JAMbo.
#include "engine/AudioCore.h"
#include "eq/EqSnapshot.h"
#include "eq/Spectrum.h"
#include "model/Project.h"
#include <cmath>
#include <cstdio>
#include <functional>
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
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

EqBand band(int type, double f, double g, double q, int slope = 0, int ms = 0) {
    return EqBand(true, type, f, g, q, slope, ms);
}

std::vector<float> render(AudioCore& core, int frames) {
    std::vector<float> out, bl(256), br(256);
    while (int(out.size()) < frames) {
        core.render(256, bl.data(), br.data());
        out.insert(out.end(), bl.begin(), bl.end());
    }
    return out;
}
double rms(const std::vector<float>& x, int a, int b) {
    double s = 0;
    for (int i = a; i < b; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    return std::sqrt(s / double(std::max(1, b - a)));
}
SampleRef sine(int frames, double hz, float amp) {
    std::vector<float> x(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) x[size_t(i)] = float(amp * std::sin(2 * M_PI * hz * double(i) / 48000));
    return std::make_shared<SampleData>(x, nullptr);
}

void edits() {
    Project p;
    const int m = EqSpec::masterChannel;
    bool ok = p.addEqBand(0, 5, 30) == 0 && p.tracks[0].eq.bands[0] == band(EqSpec::bell, 20, 18, 1);
    for (int k = 1; k < EqSpec::bands; ++k) ok = ok && p.addEqBand(0, 1000, 0) == k;
    ok = ok && p.addEqBand(0, 1000, 0) == -1;
    p.setEqBand(0, 0, 30000, -3.14159, 50);
    const auto& b0 = [&]() -> const EqBand& { return *p.tracks[0].eq.bands[0]; };
    ok = ok && b0().freq == 20000 && b0().gain == -3.1 && b0().q == 18;
    p.setEqType(0, 0, -1);
    ok = ok && b0().type == EqSpec::notch;
    p.setEqSlope(0, 0, 5);
    ok = ok && b0().slope == 2;
    p.setEqMs(0, 0, 1);
    ok = ok && b0().ms == 1;
    p.setEqMs(0, 0, 7);
    ok = ok && b0().ms == 2;
    p.toggleEqBand(0, 0);
    ok = ok && !b0().on;
    p.deleteEqBand(0, 3);
    ok = ok && !p.tracks[0].eq.bands[3] && p.addEqBand(0, 100, 0) == 3;
    p.toggleEq(0);
    ok = ok && !p.tracks[0].eq.on && p.addEqBand(m, 100, 6) == 0 && p.masterEq.bands[0]->gain == 6 &&
         !p.tracks[1].eq.bands[0];
    p.moveTrack(0, 2);
    check(ok && p.tracks[2].eq.bands[1]->freq == 1000, "eq: правки полос, мастер, переезд с дорожкой");

    std::vector<double> f(size_t(EqSpec::stride));
    Eq e;
    e.bands[0] = band(EqSpec::lowCut, 100, 6, 2, 2);
    e.bands[1] = EqBand(false, EqSpec::bell, 100, 6, 2);
    e.bands[2] = band(EqSpec::bell, 100, 6, 2, 0, 2);
    EqSpec::fill(f.data(), &e);
    const std::vector<double> head(f.begin(), f.begin() + 10);
    const std::vector<double> want{1, double(EqSpec::lowCut), 4, 100, 0, 20 * std::log10(2.0), 0, -1, -24, -6};
    bool fillOk = true;
    for (size_t i = 0; i < want.size(); ++i) fillOk = fillOk && near(head[i], want[i], 1e-12);
    fillOk = fillOk && f[size_t(1 + EqSpec::bandStride)] == -1 && f[size_t(1 + 2 * EqSpec::bandStride + 5)] == 2;
    e.on = false;
    EqSpec::fill(f.data(), &e);
    check(fillOk && f[1] == -1, "eq: снимок полос");
}

/// Синус 100 Гц на дорожке 0 с EQ edit; master — через мастер (EQ мастера).
std::vector<float> run(const std::function<void(Project&)>& edit, bool master = false, const ParamMap& sends = {}) {
    AudioCore core(master);
    Project p;
    p.tracks[0].addSamples({"s"});
    p.tracks[0].vol = 0;
    for (const auto& [id, v] : sends) p.tracks[0].params[id] = v;
    edit(p);
    core.publish(p, {{"s", sine(48000, 100, 0.5f)}});
    // Кроссфейд введения полос (40 мс) — до ноты.
    render(core, 4096);
    core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    return render(core, 36000);
}

void lowCut(Eq& e) { e.bands[0] = band(EqSpec::lowCut, 4000, 0, 0.7, 2); }

void routing() {
    const auto none = [](Project&) {};
    const double dry = rms(run(none), 12000, 24000);
    const double cut = rms(run([](Project& p) { lowCut(p.tracks[0].eq); }), 12000, 24000);
    check(dry > 0.1 && 20 * std::log10(cut / dry) < -80, "core eq дорожки: сухой сигнал");
    const double echo = rms(run(none, false, {{"delay", 100}}), 13000, 23000);
    const double echoCut = rms(run([](Project& p) { lowCut(p.tracks[0].eq); }, false, {{"delay", 100}}), 13000, 23000);
    check(echo > dry * 1.2 && 20 * std::log10(echoCut / echo) < -80, "core eq дорожки: посыл Delay");
    const auto byp = run([](Project& p) {
        lowCut(p.tracks[0].eq);
        p.tracks[0].eq.on = false;
    });
    check(near(rms(byp, 12000, 24000), rms(run(none), 12000, 24000), 1e-6), "core eq дорожки: BYPASS");
    const double mdry = rms(run(none, true), 12000, 24000);
    const double mcut = rms(run([](Project& p) { lowCut(p.masterEq); }, true), 12000, 24000);
    check(mdry > 0.1 && 20 * std::log10(mcut / mdry) < -80, "core eq мастера");
}

void ringingTail() {
    AudioCore core(false);
    Project p;
    p.tracks[0].addSamples({"k"});
    p.tracks[0].vol = 0;
    p.tracks[0].params["decay"] = 0;
    p.tracks[0].eq.bands[0] = band(EqSpec::bell, 200, 18, 18);
    core.publish(p, {{"k", sine(480, 200, 1)}});
    render(core, 4096);
    core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    const auto o = render(core, 144000);
    int last = 0;
    for (int i = int(o.size()) - 1; i >= 0; --i)
        if (std::fabs(o[size_t(i)]) > 1e-12f) {
            last = i;
            break;
        }
    check(rms(o, 9600, 14400) > 1e-3 && !core.trackEqs.ringing(0) && last > 48000 && last < 120000,
          "core eq: хвост звенит и затихает");
}

float peak(const SpectrumTap& tap) {
    SpectrumAnalyzer a;
    a.analyze(tap);
    float m = -240;
    for (int i = 0; i < SpectrumAnalyzer::bins; ++i) m = std::max(m, a.db[size_t(i)]);
    return m;
}

void taps() {
    AudioCore core(true);
    Project p;
    p.tracks[0].addSamples({"s"});
    p.tracks[0].eq.bands[0] = band(EqSpec::lowCut, 4000, 0, 0.7, 2);
    core.publish(p, {{"s", sine(48000, 100, 0.5f)}});
    core.spectra.channel.store(0);
    std::vector<float> bl(256), br(256);
    for (int i = 0; i < 16; ++i) core.render(256, bl.data(), br.data());
    core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    for (int i = 0; i < 40; ++i) core.render(256, bl.data(), br.data());
    const bool ok = peak(core.spectra.pre) > -40 && peak(core.spectra.post) < -100;
    core.spectra.channel.store(EqSpec::phonesChannel);
    for (int i = 0; i < 40; ++i) core.render(256, bl.data(), br.data());
    check(ok && peak(core.spectra.pre) < -100, "core spectra: до и после EQ, окно наушников");
}

void dyn() {
    Project p;
    const auto b = [&]() -> const EqBand& { return *p.tracks[0].eq.bands[0]; };
    bool ok = p.addEqBand(0, 100, 0) == 0 && b().dyn == -1 && b().thr == -24 && b().range == -6;
    p.setEqDyn(0, 0, 3);
    ok = ok && b().dyn == 3;
    p.setEqDyn(0, 0, 99);
    ok = ok && b().dyn == Params::maxTracks;
    p.setEqDyn(0, 0, -5);
    ok = ok && b().dyn == -1;
    p.setEqDynParams(0, 0, -70, 7.26);
    ok = ok && b().thr == -60 && b().range == 7.3;
    p.setEqDynParams(0, 0, 3, -30);
    check(ok && b().thr == 0 && b().range == -18, "eq dyn: правки");

    Eq e;
    EqBand x = band(EqSpec::highShelf, 5000, 3, 1);
    x.dyn = 2;
    x.thr = -30;
    x.range = 9;
    e.bands[0] = x;
    x.type = EqSpec::notch;
    e.bands[1] = x;
    std::vector<double> f(size_t(EqSpec::stride));
    EqSpec::fill(f.data(), &e);
    check(f[7] == 2 && f[8] == -30 && f[9] == 9 && f[size_t(1 + EqSpec::bandStride + 6)] == -1, "eq dyn: снимок");

    Project q;
    q.addEqBand(0, 100, 0);
    q.setEqDyn(0, 0, 3);
    q.addEqBand(EqSpec::masterChannel, 100, 0);
    q.setEqDyn(EqSpec::masterChannel, 0, 2);
    const auto id3 = q.tracks[2].id;
    q.moveTrack(2, 3);
    ok = q.tracks[3].id == id3 && q.tracks[0].eq.bands[0]->dyn == 4 && q.masterEq.bands[0]->dyn == 2;
    q.moveTrack(3, 0);
    ok = ok && q.tracks[0].id == id3 && q.tracks[1].eq.bands[0]->dyn == 1 && q.masterEq.bands[0]->dyn == 3;
    q.addTrack();
    q.deleteTrack(0);
    check(ok && q.tracks[0].eq.bands[0]->dyn == -1 && q.masterEq.bands[0]->dyn == 2, "eq dyn: источник за дорожкой");

    Project r;
    ok = EqSpec::mixOrder(r, 3) == std::vector<int>{0, 1, 2};
    r.addEqBand(0, 100, 0);
    r.setEqDyn(0, 0, 3);
    ok = ok && EqSpec::mixOrder(r, 3) == std::vector<int>{1, 2, 0};
    r.addEqBand(2, 100, 0);
    r.setEqDyn(2, 0, 1);
    ok = ok && EqSpec::mixOrder(r, 3) == std::vector<int>{1, 0, 2};
    r.setEqDyn(2, 0, 2);
    ok = ok && EqSpec::mixOrder(r, 3) == std::vector<int>{1, 2, 0};
    r.tracks[0].eq.on = false;
    check(ok && EqSpec::mixOrder(r, 3) == std::vector<int>{0, 1, 2}, "eq dyn: порядок микшера");
}

/// Дорожка listener — синус 100 Гц, bell с источником T2 (−12 dB, порог −40); дорожка 1 — громкий синус, заглушена.
std::pair<double, float> duck(bool source, int listener = 0) {
    AudioCore core(false);
    Project p;
    p.tracks[size_t(listener)].addSamples({"s"});
    p.tracks[size_t(listener)].vol = 0;
    p.tracks[1].addSamples({"k"});
    p.tracks[1].mute = true;
    p.addEqBand(listener, 100, 0);
    p.setEqDyn(listener, 0, 2);
    p.setEqDynParams(listener, 0, -40, -12);
    core.publish(p, {{"s", sine(48000, 100, 0.5f)}, {"k", sine(48000, 100, 0.5f)}});
    render(core, 4096);
    core.events.push(CoreEvent::noteOn(listener, 48, 1, 1));
    if (source) core.events.push(CoreEvent::noteOn(1, 48, 2, 1));
    const auto o = render(core, 24000);
    return {rms(o, 12000, 24000), core.eqDyn.offset(listener, 0)};
}

void dynSource() {
    const auto [dry, off0] = duck(false);
    const auto [ducked, off1] = duck(true);
    const auto [ducked2, off2] = duck(true, 2);
    (void)off2;
    check(dry > 0.1 && off0 == 0 && near(off1, -12, 0.01) && near(20 * std::log10(ducked / dry), -12, 0.3) &&
              near(20 * std::log10(ducked2 / dry), -12, 0.3),
          "core eq dyn: источник — дорожка (до mute), порядок микшера");
}
}  // namespace

int runEqCoreTests() {
    edits();
    routing();
    ringingTail();
    taps();
    dyn();
    dynSource();
    std::printf("eq core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
