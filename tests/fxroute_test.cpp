// Посылы, эффекты дорожек, Sidechain и p-lock FX через ядро: порт FxRoutingTests (ветка swift JAMbo).
#include "engine/AudioCore.h"
#include "fx/FxRandom.h"
#include "model/Project.h"
#include <algorithm>
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
std::vector<float> render(AudioCore& core, int frames) {
    std::vector<float> out, bl(256), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, bl.data(), br.data());
        out.insert(out.end(), bl.begin(), bl.begin() + n);
        pos += n;
    }
    return out;
}
double rms(const std::vector<float>& x, int a, int b) {
    double s = 0;
    for (int i = a; i < b; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    return std::sqrt(s / std::max(1, b - a));
}
SampleRef burst() {
    FxRandom r(5);
    std::vector<float> x(4800);
    for (auto& v : x) v = float(r.next() * 2 - 1) * 0.5f;
    return std::make_shared<SampleData>(x, nullptr);
}
SampleRef dc(int n) { return std::make_shared<SampleData>(std::vector<float>(static_cast<size_t>(n), 1.f), nullptr); }
Project project(const std::map<std::string, double>& sends) {
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].vol = 0;
    for (const auto& [k, v] : sends) p.tracks[0].params[k] = v;
    return p;
}

void sends() {
    auto tail = [](const std::map<std::string, double>& s) {
        AudioCore core(false);
        core.publish(project(s), {{"k", burst()}});
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        return rms(render(core, 30000), 12000, 30000);
    };
    check(tail({}) < 1e-6 && tail({{"reverb", 100}}) > 1e-3 && tail({{"delay", 100}}) > 1e-3,
          "fx: посылы доходят до эффектов");
    AudioCore core(false);
    Project p = project({{"reverb", 100}});
    p.tracks[0].mute = true;
    core.publish(p, {{"k", burst()}});
    core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    check(rms(render(core, 30000), 0, 30000) < 1e-5, "fx: заглушённая дорожка не посылает");
}

void sidechain() {
    AudioCore core(false);
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].mute = true;
    p.tracks[1].sample = "dc";
    p.tracks[1].vol = 0;
    p.tracks[1].params["sidechain"] = 100;
    p.fx["sc.source"] = 0;
    p.fx["sc.attack"] = 5;
    p.fx["sc.release"] = 150;
    core.publish(p, {{"k", burst()}, {"dc", dc(96000)}});
    core.events.push(CoreEvent::noteOn(1, 48, 1, 1));
    auto o = render(core, 4800);
    const float level = o[4700];
    core.events.push(CoreEvent::noteOn(0, 48, 2, 1));
    o = render(core, 9600 + 4800);
    check(level > 0.5f && o[400] < level * 0.05f && near(o[9000], level, 0.01), "fx: Sidechain приглушает и отпускает");
    for (const std::string mode : {"open", "mute", "midi", "midi+mute"}) {
        AudioCore c(false);
        Project q;
        q.tracks[0].sample = "k";
        q.tracks[0].pagesMut()[0][0].on = true;
        if (mode.find("mute") != std::string::npos) q.tracks[0].mute = true;
        if (mode.rfind("midi", 0) == 0) q.tracks[0].midi.outDevice = 1;
        q.tracks[1].sample = "dc";
        q.tracks[1].vol = 0;
        q.tracks[1].params["sidechain"] = 100;
        q.fx["sc.source"] = 0;
        q.fx["sc.attack"] = 1;
        q.fx["sc.release"] = 400;
        c.publish(q, {{"k", burst()}, {"dc", dc(480000)}});
        c.events.push(CoreEvent::noteOn(1, 48, 1, 1));
        const float before = render(c, 2400)[2300];
        c.events.push(CoreEvent::play(true));
        const auto x = render(c, 4800);
        const float low = *std::min_element(x.begin() + 600, x.begin() + 2401);
        check(before > 0.5f && low < before * 0.3f, "fx: Sidechain от паттерна, " + mode);
    }
}

void locksAndIdle() {
    {
        AudioCore core(false);
        Project p;
        p.tracks[0].pagesMut()[0][0].locks["rev.size"] = 10;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        render(core, 512);
        const int c = Params::fxCount;
        const bool a = near(core.fx.live[FxRack::revSize], 10, 1e-6) && near(core.fx.tlive[FxRack::revSize], 10, 1e-6) &&
                       near(core.fx.tlive[size_t(c + FxRack::revSize)], 30, 1e-6);
        render(core, 6000);
        check(a && near(core.fx.live[FxRack::revSize], 30, 1e-6) && Params::fxDefaults()["rev.decay"] == 1.5 &&
                  near(core.fx.live[FxRack::revHpf], Params::revHpfNew, 1e-6),
              "fx: p-lock FX на время шага");
    }
    {
        AudioCore core(false);
        core.publish(project({{"reverb", 100}, {"delay", 100}}), {{"k", burst()}});
        const bool sleep0 = core.fx.revIdle(0);
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        auto o = render(core, 24000);
        const bool tail = rms(o, 4800, 24000) > 1e-3 && !core.fx.revIdle(0);
        core.events.push(CoreEvent::noteOff(0, 1));
        render(core, 48000 * 8);
        const bool revSleep = core.fx.revIdle(0);
        render(core, 48000 * 12);
        const bool dlySleep = core.fx.dlyIdle(0);
        o = render(core, 4800);
        float mx = 0;
        for (float v : o) mx = std::max(mx, std::fabs(v));
        core.events.push(CoreEvent::noteOn(0, 48, 2, 1));
        o = render(core, 24000);
        check(sleep0 && tail && revSleep && dlySleep && mx == 0 && !core.fx.revIdle(0) && !core.fx.dlyIdle(0) &&
                  rms(o, 4800, 24000) > 1e-3,
              "fx: Reverb и Delay засыпают и просыпаются");
    }
    {
        AudioCore core(false);
        Project p = project({{"reverb", 100}});
        p.tracks[0].clips[0]->fx = ParamMap{{"rev.size", 80}, {"rev.decay", 4}};
        p.fx["rev.size"] = 20;
        core.publish(p, {{"k", burst()}});
        core.events.push(CoreEvent::play(true));
        render(core, 512);
        const int c = Params::fxCount;
        const bool v = near(core.fx.tlive[FxRack::revSize], 80, 1e-6) && near(core.fx.tlive[FxRack::revDecay], 4, 1e-6) &&
                       near(core.fx.tlive[size_t(c + FxRack::revSize)], 20, 1e-6);
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        render(core, 4800);
        check(v && !core.fx.revIdle(0) && core.fx.revIdle(1), "fx: FX дорожки — из играющего клипа");
    }
}
}  // namespace

int runFxRouteTests() {
    sends();
    sidechain();
    locksAndIdle();
    std::printf("fx routing: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
