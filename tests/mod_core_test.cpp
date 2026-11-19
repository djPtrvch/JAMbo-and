// LFO и ENV через ядро: порт ModTests (ветка swift JAMbo) — формы и ADSR без ядра уже в audio_test.cpp.
#include "engine/AudioCore.h"
#include "model/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
struct Out {
    std::vector<float> l;
    std::vector<int64_t> trigs;
};
Out render(AudioCore& core, int frames) {
    Out o;
    std::vector<float> bl(256), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, bl.data(), br.data());
        o.l.insert(o.l.end(), bl.begin(), bl.begin() + n);
        pos += n;
        while (auto m = core.sequencer.out.pop())
            if (m->kind == SeqOut::Kind::trig) o.trigs.push_back(m->frame);
    }
    return o;
}

void envelope() {
    AudioCore core;
    core.publish(Project());
    render(core, 256);
    ModEngine& m = core.mod;
    m.trig(0, 1000, 48000);
    check(near(m.env(0, 1000), 0, 1e-9) && near(m.env(0, 1240), 0.5, 1e-9) && near(m.env(0, 1000 + 480 + 2400), 0.7, 1e-9) &&
              near(m.env(0, 21000), 0.4, 1e-9) && near(m.env(0, 49000 + 7200), 0.2, 1e-9) &&
              near(m.env(0, 69000), 0, 1e-9),
          "mod: ADSR в ядре");
}

void lfoOnLevel() {
    AudioCore core;
    Project p;
    p.tracks[0].sample = "dc";
    p.tracks[0].vol = 0;
    p.tracks[0].params["lfo1.wave"] = 3;
    p.tracks[0].params["lfo1.speed"] = 1;
    p.tracks[0].assigns["level"] = ModAssign(1, -1);
    core.publish(p, {{"dc", std::make_shared<SampleData>(std::vector<float>(96000, 1.f), nullptr)}});
    core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    const Out o = render(core, 48000);
    check(std::fabs(o.l[12000]) < 0.01f && near(o.l[36000], std::cos(M_PI / 4), 0.01), "mod: LFO на Level");
}

void lfoOnChance() {
    AudioCore core;
    Project p;
    for (int s = 0; s < 16; ++s) {
        p.tracks[0].pagesMut()[0][size_t(s)].on = true;
        p.tracks[0].pagesMut()[0][size_t(s)].notes = {48};
    }
    p.tracks[0].params["lfo1.wave"] = 3;
    p.tracks[0].params["lfo1.speed"] = 0.5;
    p.tracks[0].assigns["prob"] = ModAssign(1, -1);
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    const Out o = render(core, 96000 * 2);
    check(std::all_of(o.trigs.begin(), o.trigs.end(), [](int64_t f) { return f >= 48000; }) && o.trigs.size() > 10,
          "mod: LFO на Chance");
}

void lfoOnFx() {
    AudioCore core;
    Project p;
    p.tracks[0].params["lfo1.wave"] = 3;
    p.tracks[0].params["lfo1.speed"] = 1;
    p.fx["rev.size"] = 50;
    p.fxAssigns["rev.size"] = ModAssign(1, -0.5, 0);
    core.publish(p);
    render(core, 4096);
    const bool lo = near(core.fx.live[FxRack::revSize], 0, 1e-6);
    render(core, 24000);
    check(lo && near(core.fx.live[FxRack::revSize], 100, 1e-6), "mod: LFO на ручке FX");
}

void envFromSequencer() {
    AudioCore core;
    Project p;
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].notes = {48};
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    render(core, 512);
    check(core.mod.outs[3].load() > 0.5f, "mod: ENV от ноты секвенсора");
}
}  // namespace

int runModCoreTests() {
    envelope();
    lfoOnLevel();
    lfoOnChance();
    lfoOnFx();
    envFromSequencer();
    std::printf("mod core: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
