// AudioCore: порт тестов Swift-версии через ядро (ветка swift JAMbo) — SamplerTests (ядро), MasterTests
// (testAudioCoreThroughMaster), LevelsTests (пики дорожек и мастера, громкость выхода, p-lock MIX, телеметрия).
#include "engine/AudioCore.h"
#include "fx/FxRandom.h"
#include "model/Project.h"
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
const float center = float(std::cos(M_PI / 4));
using Samples = std::map<std::string, SampleRef>;
SampleRef mono(const std::vector<float>& x) { return std::make_shared<SampleData>(x, nullptr); }
SampleRef dc(int n = 48000) { return mono(std::vector<float>(static_cast<size_t>(n), 1.f)); }
SampleRef noise(uint32_t seed, float amp, int n = 96000) {
    FxRandom r(seed);
    std::vector<float> x(static_cast<size_t>(n));
    for (auto& v : x) v = float(r.next() * 2 - 1) * amp;
    return mono(x);
}
Project project(const std::string& key = "k") {
    Project p;
    p.tracks[0].sample = key;
    p.tracks[0].vol = 0;
    return p;
}
struct Out {
    std::vector<float> l, r;
};
Out render(AudioCore& core, int frames) {
    Out o;
    std::vector<float> bl(256), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, bl.data(), br.data());
        o.l.insert(o.l.end(), bl.begin(), bl.begin() + n);
        o.r.insert(o.r.end(), br.begin(), br.begin() + n);
        pos += n;
    }
    return o;
}

void oneStep() {
    AudioCore core(false);
    Project p = project();
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].notes = {48};
    core.publish(p, {{"k", dc()}});
    core.events.push(CoreEvent::play(true));
    const Out o = render(core, 20000);
    check(near(o.l[24], center * 0.5, 0.02) && near(o.l[48], center, 0.01) &&
              near(o.l[48 + 6000], center * std::exp(-1.0), 0.01) && near(o.l[100], o.r[100], 1e-6),
          "core: шаг — атака и Decay");
}

void liveAndMute() {
    AudioCore core(false);
    Project p = project();
    SampleRef s = dc();
    core.publish(p, {{"k", s}});
    core.events.push(CoreEvent::noteOn(0, 48, 7, 1));
    Out o = render(core, 12000);
    const bool held = near(o.l[11000], center, 0.01);
    core.events.push(CoreEvent::noteOff(0, 7));
    o = render(core, 6000);
    check(held && near(o.l[5999], center * std::exp(-1.0), 0.02), "core: живая нота и отпускание");
    AudioCore m(false);
    m.publish(p, {{"k", s}});
    m.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    render(m, 2048);
    p.tracks[0].mute = true;
    m.publish(p, {{"k", s}});
    o = render(m, 1024);
    check(std::fabs(o.l[480]) < 0.001f && std::fabs(o.l[0]) > 0.3f && m.sampler.activeCount() == 1,
          "core: mute — гейт за 10 мс");
}

void midiOut() {
    AudioCore core(false);
    Project p = project();
    p.tracks[0].midi.outDevice = 5;
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].notes = {48};
    p.tracks[0].pagesMut()[0][0].len = 2;
    core.publish(p, {{"k", dc()}});
    core.events.push(CoreEvent::play(true));
    const Out o = render(core, 2000);
    float pk = 0;
    for (float v : o.l) pk = std::max(pk, std::fabs(v));
    auto e = core.sampler.midiOut.pop();
    check(pk < 1e-6f && e && e->kind == MidiNoteOut::on && e->length == 12000, "core: MIDI OUT дорожки");
    core.events.push(CoreEvent::noteOn(0, 60, 9, 0.5f));
    core.events.push(CoreEvent::noteOff(0, 9));
    core.events.push(CoreEvent::play(false));
    render(core, 256);
    std::vector<uint8_t> kinds;
    while (auto x = core.sampler.midiOut.pop()) kinds.push_back(x->kind);
    check(kinds == std::vector<uint8_t>{MidiNoteOut::on, MidiNoteOut::release, MidiNoteOut::allOff},
          "core: MIDI OUT — живая, отпускание, стоп");
}

void moveAndRebind() {
    for (int mode = 0; mode < 4; ++mode) {
        AudioCore core(false);
        Project p = project();
        p.tracks[0].pagesMut()[0][0].on = true;
        p.tracks[0].pagesMut()[0][0].notes = {48};
        p.tracks[0].pagesMut()[0][0].len = 16;
        SampleRef k = dc();
        core.publish(p, {{"k", k}});
        core.events.push(CoreEvent::play(true));
        const Out a = render(core, 3000);
        SoundSnapshot::Options o;
        if (mode == 3) {
            core.publish(p, {{"k", k}});
        } else if (mode == 0) {
            p.moveTrack(0, 3);
            core.publish(p, {{"k", k}});
            core.events.push(CoreEvent::moveTrack(0, 3));
        } else {
            for (auto& t : p.tracks) t.id += 1000;
            o.rebind = mode == 1;
            core.publish(p, {{"k", k}}, o);
        }
        const Out b = render(core, 1024);
        const bool ok = mode == 2 ? std::fabs(b.l[1023]) < 0.01f : std::fabs(b.l[1023]) > 0.3f;
        check(std::fabs(a.l[2999]) > 0.3f && ok, "core: перестановка и MIX, режим " + std::to_string(mode));
    }
}

void synthMoves() {
    AudioCore core(false);
    core.installSynth();
    Project p;
    p.tracks[0].sample = "mi:17";
    p.tracks[0].vol = 0;
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].notes = {60};
    p.tracks[0].pagesMut()[0][0].len = 16;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    const Out a = render(core, 3000);
    float pk0 = 0, pk = 0;
    for (int i = 2000; i < 3000; ++i) pk0 = std::max(pk0, std::fabs(a.l[size_t(i)]));
    p.moveTrack(0, 3);
    core.publish(p);
    core.events.push(CoreEvent::moveTrack(0, 3));
    const Out b = render(core, 2048);
    for (int i = 1024; i < 2048; ++i) pk = std::max(pk, std::fabs(b.l[size_t(i)]));
    check(pk0 > 0.05f && pk > 0.05f, "core: синт — нота на новом месте");
}

void preview() {
    AudioCore core(false);
    core.publish(project(), {{"k", dc()}});
    core.events.push(CoreEvent::preview(SoundKey::hash("k"), 0, 1));
    Out o = render(core, 1000);
    const bool on = near(o.l[500], Sampler::previewGain, 1e-5);
    core.events.push(CoreEvent::make(CoreEvent::Kind::previewStop));
    o = render(core, 4000);
    check(on && std::fabs(o.l[3999]) < 0.001f && !core.sampler.previewing(), "core: предпрослушка");
}

void throughMaster() {
    auto run = [](bool master, auto edit) {
        AudioCore core(master);
        Project p = project();
        edit(p);
        core.publish(p, {{"k", dc()}});
        std::vector<float> out, bl(256), br(256);
        for (int i = 0; i < 8; ++i) core.render(256, bl.data(), br.data());
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        for (int i = 0; i < 40; ++i) {
            core.render(256, bl.data(), br.data());
            out.insert(out.end(), bl.begin(), bl.end());
        }
        return out;
    };
    auto first = [](const std::vector<float>& x) {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i]) > 0.3f) return int(i);
        return -1;
    };
    auto peakOf = [](const std::vector<float>& x) {
        float m = 0;
        for (float v : x) m = std::max(m, std::fabs(v));
        return m;
    };
    const auto dry = run(false, [](Project&) {});
    const auto wet = run(true, [](Project&) {});
    check(first(wet) - first(dry) == 219, "core: задержка мастера 219");
    const auto hot = run(true, [](Project& p) { p.master["lim.input"] = 24; });
    check(peakOf(hot) <= float(std::pow(10.0, -1.0 / 20)) + 1e-5f && peakOf(hot) > 0.85f, "core: лимитер мастера");
    const auto byp = run(true, [](Project& p) {
        p.satBypass = p.cmpBypass = p.clipBypass = p.limBypass = true;
    });
    check(first(byp) - first(dry) == 219 && near(byp[size_t(first(byp) + 3000)], dry[size_t(first(dry) + 3000)], 1e-5),
          "core: BYPASS мастера — задержка та же");
}

struct Pk {
    float l = 0, r = 0;
};
Pk peak(const AudioCore& core, int ch) {
    Pk p;
    core.levels.read(0, [&](const float* b) {
        p.l = std::max(p.l, Levels::value(b, ch, 0));
        p.r = std::max(p.r, Levels::value(b, ch, 1));
    });
    return p;
}

void levels() {
    for (bool mute : {false, true}) {
        AudioCore core(false);
        Project p = project("dc");
        p.tracks[0].vol = -6;
        p.tracks[0].mute = mute;
        core.publish(p, {{"dc", mono(std::vector<float>(96000, 0.5f))}});
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        render(core, 9600);
        const Pk a = peak(core, 0);
        check(a.l > 0.1f && near(a.l, a.r, 1e-4) && peak(core, 1).l == 0, "levels: пик дорожки до mute");
    }
    {
        AudioCore core;
        core.publish(project("dc"), {{"dc", noise(7, 0.3f)}});
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        render(core, 24000);
        const Pk m = peak(core, Levels::master);
        check(m.l > 0.05f && m.r > 0.05f && m.l < 1.01f, "levels: пик мастера");
    }
    {
        const SampleRef n = noise(5, 0.3f);
        auto run = [&](double db, AudioCore& core, Project& p) {
            p = project("n");
            p.outVol = db;
            core.publish(p, {{"n", n}});
            core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
            render(core, 24000);
            return peak(core, Levels::master).l;
        };
        AudioCore c0, c1;
        Project p0, p1;
        const float a = run(0, c0, p0);
        float b = run(-6, c1, p1);
        const bool ok = a > 0.05f && near(b / a, Params::volGain(-6), 0.01);
        p1.outVol = Params::volMin;
        c1.publish(p1, {{"n", n}});
        render(c1, 24000);
        b = peak(c1, Levels::master).l;
        check(ok && b < 1e-5f, "levels: громкость выхода");
    }
    {
        AudioCore core;
        Project p = project("n");
        p.chanSeqs[Project::mixSeq].setLength(1);
        p.chanSeqs[Project::mixSeq].setLock(0, "mix.vol1", Params::volMin);
        core.publish(p, {{"n", noise(3, 0.3f)}});
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        render(core, 24000);
        const bool before = peak(core, 0).l > 0.05f;
        core.events.push(CoreEvent::play(true));
        render(core, 24000);
        check(before && peak(core, 0).l < 1e-4f, "levels: p-lock громкости MIX");
    }
    {
        AudioCore core;
        Project p = project("n");
        p.master["clip.drive"] = 12;
        core.publish(p, {{"n", noise(9, 0.5f)}});
        core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
        render(core, 24000);
        const auto c = core.master.clipTele.peaks(0);
        const auto l = core.master.limTele.peaks(0);
        check(near(double(core.master.limTele.written.load()), 24000 / 256 - 1, 2) && c.inPeak > 1 && c.gr > 1 &&
                  l.outPeak < 1.01f && core.master.compTele.peaks(0).inPeak > 0.05f,
              "levels: телеметрия COMP / CLIP / LIM");
    }
}
/// Проверочный проект APK (audio_engine.cpp): DRUM и ANALOG через ядро с мастером — звучат, без NaN, не выше 0 dBFS.
void demoProject() {
    AudioCore core;
    core.installSynth();
    Project p;
    p.tracks[0].setSynth(16);
    p.tracks[1].setSynth(17);
    for (int s : {0, 4, 8, 12}) {
        p.tracks[0].pagesMut()[0][size_t(s)].on = true;
        p.tracks[0].pagesMut()[0][size_t(s)].notes = {48};
    }
    const int notes[4] = {36, 36, 43, 41};
    for (int k = 0; k < 4; ++k) {
        p.tracks[1].pagesMut()[0][size_t(k * 4 + 2)].on = true;
        p.tracks[1].pagesMut()[0][size_t(k * 4 + 2)].notes = {notes[k]};
    }
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    const Out o = render(core, 48000 * 2);
    float pk = 0;
    bool finite = true;
    for (size_t i = 0; i < o.l.size(); ++i) {
        pk = std::max({pk, std::fabs(o.l[i]), std::fabs(o.r[i])});
        finite = finite && std::isfinite(o.l[i]) && std::isfinite(o.r[i]);
    }
    core.events.push(CoreEvent::noteOn(1, 60, 60, 1));
    const Out n = render(core, 4800);
    float pn = 0;
    for (float v : n.l) pn = std::max(pn, std::fabs(v));
    check(finite && pk > 0.05f && pk <= 1.0f && pn > 0.05f, "core: проверочный проект APK звучит");
}
}  // namespace

/// Скоп мастера (LevelsTests.testScopeTaps): включённые точки съёма пишутся, выключенные — нет.
void scopeTaps() {
    AudioCore core;
    Project p;
    p.tracks[0].addSamples({"n"});
    p.tracks[0].vol = 0;
    FxRandom rnd(11);
    std::vector<float> noise(96000);
    for (auto& v : noise) v = float(rnd.next() * 2 - 1);
    core.publish(p, {{"n", mono(noise)}});
    auto& sc = core.master.scope;
    sc.mask.store(1 << MasterScope::sat | 1 << MasterScope::lim | MasterScope::gonioBit);
    core.events.push(CoreEvent::noteOn(0, 48, 1, 1));
    std::vector<float> l(256), r(256);
    for (int pos = 0; pos < 9600; pos += 256) core.render(std::min(256, 9600 - pos), l.data(), r.data());
    std::vector<float> buf(4096);
    sc.taps[MasterScope::lim].read(buf.data(), 4096, 9600);
    float m = 0;
    for (float v : buf) m = std::max(m, std::fabs(v));
    check(sc.taps[MasterScope::sat].written.load() == 9600 && sc.taps[MasterScope::comp].written.load() == 0 &&
              sc.gonioL.written.load() == 9600 && m < 0.9f && m > 0.1f,
          "scope: точки съёма мастера");
}

int runCoreTests() {
    demoProject();
    oneStep();
    liveAndMute();
    midiOut();
    moveAndRebind();
    synthMoves();
    preview();
    throughMaster();
    levels();
    scopeTaps();
    std::printf("core: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
