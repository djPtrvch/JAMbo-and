// Сэмплер: порт SamplerTests, RateTests (звук), WarpTests (звук), PvFFTTests, VocoderLoadTests (ветка swift JAMbo) —
// без AudioCore: снимок звука → Sampler::adopt, ноты — noteOn / секвенсор, блок — Sampler::render + renderPreview.
#include "model/Project.h"
#include "sampler/PvFFT.h"
#include "sampler/Sampler.h"
#include "sampler/SoundSnapshot.h"
#include "sequencer/SeqSnapshot.h"
#include "sequencer/Sequencer.h"
#include "synth/SynthVoices.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
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
SampleRef dc(int frames = 48000) { return mono(std::vector<float>(size_t(frames), 1.f)); }

Project project(const std::string& key = "k") {
    Project p;
    p.tracks[0].sample = key;
    p.tracks[0].vol = 0;
    return p;
}

struct Rig {
    Sampler s{48000};
    std::vector<std::unique_ptr<SoundSnapshot>> snaps;
    std::unique_ptr<SeqSnapshot> seqSnap;
    std::unique_ptr<Sequencer> seq;
    std::unique_ptr<SynthVoices> synth;
    int64_t frame = 0;
    std::vector<float> l, r;

    void publish(const Project& p, const Samples& smp, SoundSnapshot::Options o = {}) {
        snaps.push_back(std::make_unique<SoundSnapshot>(p, smp, o));
        s.bpm = p.bpm;
        s.adopt(snaps.back()->mutableData(), frame);
        if (seq) {
            seqSnap = std::make_unique<SeqSnapshot>(p);
            seq->setData(&seqSnap->data());
        }
    }
    void withSeq(const Project& p) {
        seq = std::make_unique<Sequencer>(48000, p.bpm);
        seq->sampler = &s;
    }
    void play(bool on) { seq->setPlaying(on, frame); }
    void run(int frames) {
        l.assign(size_t(frames), 0.f);
        r.assign(size_t(frames), 0.f);
        std::vector<float> bl(256), br(256);
        int pos = 0;
        while (pos < frames) {
            const int n = std::min(256, frames - pos);
            std::fill(bl.begin(), bl.end(), 0.f);
            std::fill(br.begin(), br.end(), 0.f);
            if (seq) {
                std::vector<float> cl(256), cr(256);
                seq->process(frame, n, cl.data(), cr.data());
            }
            s.render(frame, n, bl.data(), br.data());
            s.renderPreview(n, bl.data(), br.data());
            std::copy(bl.begin(), bl.begin() + n, l.begin() + pos);
            std::copy(br.begin(), br.begin() + n, r.begin() + pos);
            frame += n;
            pos += n;
        }
    }
};

void oneStepNoteAttackAndDecay() {
    Project p = project();
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].notes = {48};
    Rig g;
    g.withSeq(p);
    g.publish(p, {{"k", dc()}});
    g.play(true);
    g.run(20000);
    check(near(g.l[24], center * 0.5, 0.02), "sampler: середина атаки");
    check(near(g.l[48], center, 0.01), "sampler: моно по центру");
    check(near(g.l[48 + 6000], center * std::exp(-1.0), 0.01), "sampler: Decay как Release");
    check(near(g.l[100], g.r[100], 1e-6), "sampler: L = R");
}

void midiOutTrack() {
    Project p = project();
    p.tracks[0].midi.outDevice = 5;
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].notes = {48};
    p.tracks[0].pagesMut()[0][0].len = 2;
    Rig g;
    g.withSeq(p);
    g.publish(p, {{"k", dc()}});
    g.play(true);
    g.run(2000);
    float pk = 0;
    for (float x : g.l) pk = std::max(pk, std::fabs(x));
    check(pk < 1e-6f, "midi out: свой звук молчит");
    auto e = g.s.midiOut.pop();
    check(e && e->kind == MidiNoteOut::on && e->note == 48 && e->frame == 0 && e->length == 12000,
          "midi out: нота шага, длина 2 шага");
    check(e && near(e->velocity, Params::byId("level")->def, 0.5), "midi out: velocity — Level");
    g.s.noteOn(0, 60, 9, 0.5f, g.frame);
    g.s.release(0, 9, g.frame);
    g.play(false);
    g.run(256);
    std::vector<uint8_t> kinds;
    std::optional<MidiNoteOut> live;
    while (auto x = g.s.midiOut.pop()) {
        kinds.push_back(x->kind);
        if (x->kind == MidiNoteOut::on) live = x;
    }
    check(kinds == std::vector<uint8_t>{MidiNoteOut::on, MidiNoteOut::release, MidiNoteOut::allOff},
          "midi out: живая нота, отпускание, всё отпустить");
    check(live && live->length == -1 && live->liveId == 9, "midi out: живая — до отпускания");
    p.tracks[0].mute = true;
    g.publish(p, {{"k", dc()}});
    g.s.noteOn(0, 60, 10, 1, g.frame);
    g.run(256);
    check(!g.s.midiOut.pop(), "midi out: заглушённая дорожка молчит");
}

void liveNoteHolds() {
    Rig g;
    g.publish(project(), {{"k", dc()}});
    g.s.noteOn(0, 48, 7, 1, 0);
    g.run(12000);
    check(near(g.l[11000], center, 0.01), "live: держит уровень");
    g.s.release(0, 7, g.frame);
    g.run(6000);
    check(near(g.l[5999], center * std::exp(-1.0), 0.02), "live: отпустили — Decay");
}

void pitchStartEndVolume() {
    Project p = project();
    p.tracks[0].params["start"] = 50;
    p.tracks[0].params["decay"] = 4000;
    p.tracks[0].vol = -10;
    Rig g;
    g.publish(p, {{"k", dc()}});
    g.s.noteOn(0, 60, 1, 1, 0);
    g.run(14000);
    const float gg = center * float(std::pow(10.0, -10.0 / 20));
    check(near(g.l[6000], gg, 0.005), "pitch: громкость микшера −10 dB");
    check(near(g.l[11999 - 300], gg, 0.01), "pitch: октава вверх — вдвое быстрее");
    check(near(g.l[12500], 0, 1e-6), "pitch: конец сэмпла — замолчал");
    check(std::fabs(g.l[11998]) < gg * 0.2f, "pitch: край гаснет за 4 мс");
    check(g.s.activeCount() == 0, "pitch: голосов нет");
}

void velocityLevelAndPan() {
    Project p = project();
    p.tracks[0].params["pan"] = 64;
    Rig g;
    g.publish(p, {{"k", dc()}});
    g.s.noteOn(0, 48, 1, 0.5f, 0);
    g.run(2000);
    check(near(g.r[1000], std::pow(0.5, 1.5), 0.01) && near(g.l[1000], 0, 0.001), "velocity и pan");
}

void muteGateClosesFast() {
    Project p = project();
    SampleRef s = dc();
    Rig g;
    g.publish(p, {{"k", s}});
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(2048);
    p.tracks[0].mute = true;
    g.publish(p, {{"k", s}});
    g.run(1024);
    check(std::fabs(g.l[480]) < 0.001f && std::fabs(g.l[0]) > 0.3f, "mute: гейт за 10 мс, без щелчка");
    check(g.s.activeCount() == 1, "mute: нота играет дальше");
}

void slicesPlayMarkedPart() {
    Project p = project();
    std::vector<float> l(48000, 0.5f);
    for (int i = 24000; i < 48000; ++i) l[size_t(i)] = 1;
    p.tracks[0].slices["k"] = {SliceMark(0), SliceMark(0.5)};
    p.tracks[0].params["decay"] = 4000;
    Rig g;
    g.publish(p, {{"k", mono(l)}});
    g.s.noteOn(0, 49, 1, 1, 0);
    g.s.noteOn(0, 60, 2, 1, 0);
    g.run(30000);
    check(near(g.l[1000], center, 0.01), "slices: вторая половина; нота без маркера молчит");
    check(near(g.l[25000], 0, 1e-6), "slices: до конца сэмпла");
}

void voiceLimit() {
    Rig g;
    g.publish(project(), {{"k", dc()}});
    for (int k = 0; k < 60; ++k) g.s.noteOn(0, 48 + k % 12, k, 1, 0);
    g.run(256);
    check(g.s.activeCount() <= 64, "voices: не больше ячеек");
    g.run(2048);
    check(g.s.activeCount() == Sampler::maxVoices, "voices: лишние погасли");
}

void soundChangeRetriggers() {
    Project p = project();
    p.tracks[0].folder.items = {FolderItem::ofSample("k"), FolderItem::ofSample("k2")};
    Samples smp{{"k", dc()}, {"k2", mono(std::vector<float>(48000, 0.5f))}};
    Rig g;
    g.publish(p, smp);
    g.s.noteOn(0, 48, 3, 1, 0);
    g.run(4000);
    p.tracks[0].sample = "k2";
    g.publish(p, smp);
    g.run(4000);
    check(near(g.l[3000], center * 0.5, 0.01), "retrigger: держащаяся нота — новым звуком");
    check(g.s.activeCount() == 1, "retrigger: один голос");
    g.s.release(0, 3, g.frame);
    g.run(48000);
    check(g.s.activeCount() == 0, "retrigger: отпускание действует на новый голос");
}

void sequencerLockAndHold() {
    Project p = project();
    Step& st = p.tracks[0].pagesMut()[0][0];
    st.on = true;
    st.notes = {48};
    st.len = 2;
    st.locks["level"] = 127 * std::pow(0.25, 1 / 1.5);
    Rig g;
    g.withSeq(p);
    g.publish(p, {{"k", dc(96000)}});
    g.play(true);
    g.run(13000);
    check(near(g.l[100], center * 0.25, 0.005) && near(g.l[11900], center * 0.25, 0.005), "seq: p-lock Level, удержание");
    check(g.l[12999] < center * 0.25f * 0.995f, "seq: после удержания — Decay");
}

void stepSoundOwnSample() {
    Project p = project();
    Step& st = p.tracks[0].pagesMut()[0][0];
    st.on = true;
    st.notes = {48};
    st.sound = "k2";
    Rig g;
    g.withSeq(p);
    g.publish(p, {{"k", dc()}, {"k2", mono(std::vector<float>(48000, 0.5f))}});
    g.play(true);
    g.run(1000);
    check(near(g.l[48], center * 0.5, 0.01), "step sound: свой сэмпл шага");
}

void voiceSurvivesMoveAndRebind() {
    SampleRef k = dc();
    for (int mode = 0; mode < 4; ++mode) {
        Project p = project();
        Rig g;
        g.publish(p, {{"k", k}});
        g.s.noteOn(0, 48, 1, 1, 0);
        g.run(3000);
        const bool before = std::fabs(g.l[2999]) > 0.3f;
        SoundSnapshot::Options o;
        if (mode == 0) {
            p.moveTrack(0, 3);
        } else if (mode != 3) {
            for (auto& t : p.tracks) t.id += 1000;
            o.rebind = mode == 1;
        }
        g.publish(p, {{"k", k}}, o);
        g.run(1024);
        if (mode == 2)
            check(before && std::fabs(g.l[1023]) < 0.01f, "move: новые id без rebind — гаснет");
        else
            check(before && std::fabs(g.l[1023]) > 0.3f, "move: режим " + std::to_string(mode) + " — звучит дальше");
    }
}

void synthNoteMovesWithTrack() {
    Project p;
    p.tracks[0].sample = "mi:17";
    p.tracks[0].vol = 0;
    Rig g;
    g.synth = std::make_unique<SynthVoices>(48000);
    g.s.synth = g.synth.get();
    g.publish(p, {});
    g.s.noteOn(0, 60, 1, 1, 0);
    g.run(3000);
    float pk0 = 0;
    for (int i = 2000; i < 3000; ++i) pk0 = std::max(pk0, std::fabs(g.l[size_t(i)]));
    p.moveTrack(0, 3);
    g.publish(p, {});
    g.run(2048);
    float pk = 0;
    for (int i = 1024; i < 2048; ++i) pk = std::max(pk, std::fabs(g.l[size_t(i)]));
    check(pk0 > 0.05f && pk > 0.05f, "synth: нота звучит на новом месте");
}

void previewPlaysAndStops() {
    Rig g;
    g.publish(project(), {{"k", dc()}});
    g.s.preview(SoundKey::hash("k"), 0, 1, 0);
    g.run(1000);
    check(near(g.l[500], Sampler::previewGain, 1e-5), "preview: играет");
    g.s.stopPreview(g.frame);
    g.run(4000);
    check(std::fabs(g.l[3999]) < 0.001f && !g.s.previewing(), "preview: стоп");
}

// RateTests.
SampleRef sine(double hz, double seconds) {
    const int n = int(seconds * 48000);
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) x[size_t(i)] = float(0.5 * std::sin(2 * M_PI * hz * double(i) / 48000));
    return mono(x);
}
Project rateProject(double rate, bool tstr, double bpm = 120) {
    Project p = project();
    p.tracks[0].params["decay"] = 4000;
    p.tracks[0].params["rate"] = rate;
    p.tracks[0].params[Params::rateModeId] = tstr ? 1 : 0;
    p.bpm = bpm;
    return p;
}
int crossings(const std::vector<float>& x, int from, int count) {
    int c = 0;
    for (int i = from + 1; i < from + count; ++i)
        if (x[size_t(i - 1)] < 0 && x[size_t(i)] >= 0) ++c;
    return c;
}
float rms(const std::vector<float>& x, int from, int count) {
    double s = 0;
    for (int i = from; i < from + count; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    return float(std::sqrt(s / count));
}
int lastSound(const std::vector<float>& x) {
    int i = int(x.size()) - 1;
    while (i > 0 && std::fabs(x[size_t(i)]) < 1e-4f) --i;
    return i;
}

void pitchModeHalfSpeed() {
    Rig g;
    g.publish(rateProject(50, false), {{"k", sine(441, 1)}});
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(110000);
    check(near(crossings(g.l, 20000, 48000), 220, 2) && near(lastSound(g.l), 96000, 300), "rate: PTCH 50 %");
}

void pitchModeReverse() {
    const int n = 48000;
    std::vector<float> ramp(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) ramp[size_t(i)] = float(i) / float(n);
    Rig g;
    g.publish(rateProject(-100, false), {{"k", mono(ramp)}});
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(50000);
    check(near(g.l[2000] / center, float(n - 2001) / float(n), 0.01) && g.l[2000] > g.l[30000] &&
              near(lastSound(g.l), n, 200) && g.s.activeCount() == 0,
          "rate: PTCH реверс");
}

void stretchKeepsPitch(bool beat) {
    SoundSnapshot::Options o;
    o.stretchBeat = beat;
    Rig g;
    g.publish(rateProject(50, true), {{"k", sine(441, 2)}}, o);
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(200000);
    const std::string nm = beat ? "BEAT" : "NORMAL";
    check(near(crossings(g.l, 20000, 48000), 441, 3) && near(crossings(g.l, 120000, 48000), 441, 3),
          "rate: TSTR " + nm + " — высота прежняя");
    check(near(lastSound(g.l), 192000, 600), "rate: TSTR " + nm + " — вдвое дольше");
    const float a = rms(g.l, 30000, 4800), b = rms(g.l, 100000, 4800);
    check(near(a, b, a * 0.05) && near(a, 0.5 / std::sqrt(2.0) * center, 0.02), "rate: TSTR " + nm + " — ровный уровень");
}

void stretchFollowsTempoAndPitch(bool beat) {
    Project p = rateProject(100, true, 60);
    p.tracks[0].params["pitch"] = 12;
    SoundSnapshot::Options o;
    o.stretchBeat = beat;
    Rig g;
    g.publish(p, {{"k", sine(441, 2)}}, o);
    g.run(256);
    g.s.noteOn(0, 48, 1, 1, g.frame);
    g.run(200000);
    const std::string nm = beat ? "BEAT" : "NORMAL";
    check(near(crossings(g.l, 20000, 48000), 882, 4) && near(lastSound(g.l), 192000, 600),
          "rate: TSTR " + nm + " — темп проекта и Pitch");
}

void stretchReverse(bool beat) {
    SoundSnapshot::Options o;
    o.stretchBeat = beat;
    Rig g;
    g.publish(rateProject(-100, true), {{"k", sine(441, 2)}}, o);
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(100000);
    check(near(crossings(g.l, 20000, 48000), 441, 3) && near(lastSound(g.l), 96000, 600),
          std::string("rate: TSTR реверс ") + (beat ? "BEAT" : "NORMAL"));
}

void vocoderNearIdentity() {
    const int n = 96000;
    std::vector<float> lf(static_cast<size_t>(n)), rf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double x = double(i) / 48000;
        lf[size_t(i)] = float(0.2 * std::sin(2 * M_PI * 220 * x) + 0.2 * std::sin(2 * M_PI * 277.2 * x) +
                              0.2 * std::sin(2 * M_PI * 329.6 * x));
        rf[size_t(i)] = float(0.2 * std::sin(2 * M_PI * 220 * x + 1) + 0.2 * std::sin(2 * M_PI * 277.2 * x + 2) +
                              0.2 * std::sin(2 * M_PI * 329.6 * x));
    }
    Project p = rateProject(100, true);
    p.tracks[0].params["attack"] = 0;
    auto s = std::make_shared<SampleData>(lf, &rf);
    Rig g;
    g.publish(p, {{"k", s}});
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(60000);
    double err = 0, sig = 0;
    for (int i = 4000; i < 58000; ++i) {
        const double a = g.l[size_t(i)] - lf[size_t(i)], b = g.r[size_t(i)] - rf[size_t(i)];
        err += a * a + b * b;
        sig += double(lf[size_t(i)]) * lf[size_t(i)] + double(rf[size_t(i)]) * rf[size_t(i)];
    }
    const double snr = 10 * std::log10(sig / std::max(1e-12, err));
    check(s->onsets().count() == 0, "vocoder: у ровного аккорда атак нет");
    check(snr > 30, "vocoder: NORMAL в темпе файла — почти без искажений (" + std::to_string(snr) + " dB)");
    check(near(g.l[100], lf[100], 0.02), "vocoder: начало ноты без задержки");
}

std::vector<float> clicks(int n, int every) {
    std::vector<float> x(size_t(n), 0.f);
    for (int k = 0; k < n; k += every)
        for (int i = 0; i < 480 && k + i < n; ++i)
            x[size_t(k + i)] = float(0.8 * std::exp(-double(i) / 60) * std::sin(double(i) * 0.9));
    return x;
}

void vocoderKeepsHits() {
    const std::vector<float> x = clicks(96000, 12000);
    auto s = mono(x);
    const OnsetList& ol = s->onsets();
    bool at = ol.count() == 7;
    for (int i = 0; at && i < ol.count(); ++i) at = near(ol.at[size_t(i)], (i + 1) * 12000 - 48, 16);
    check(at, "vocoder: атаки сэмпла — за 1 мс до удара");
    Rig g;
    g.publish(rateProject(50, true), {{"k", s}});
    g.s.noteOn(0, 48, 1, 1, 0);
    g.run(192000);
    bool ok = true;
    for (int hit : {24000, 48000, 72000, 96000}) {
        const float on = rms(g.l, hit - 240, 1200), off = rms(g.l, hit + 6000, 6000), pre = rms(g.l, hit - 1440, 1200);
        ok = ok && on > off * 8 && on > pre * 30;
    }
    check(ok, "vocoder: удары короткие, без предэха");
}

// WarpTests (звук): метки — как у autoWarp с сеткой 1 (правки варпинга Track не перенесены).
std::vector<int> hitTimes(const std::vector<float>& o) {
    std::vector<int> out;
    int quiet = 0;
    for (int i = 0; i < int(o.size()); ++i) {
        if (std::fabs(o[size_t(i)]) > 0.05f) {
            if (quiet > 2400 || (out.empty() && quiet == i)) out.push_back(i);
            quiet = 0;
        } else {
            ++quiet;
        }
    }
    return out;
}
SampleRef hits() {
    std::vector<float> x(96000, 0.f);
    for (int a : {0, 24000, 52800, 72000})
        for (int i = 0; i < 480 && a + i < 96000; ++i)
            x[size_t(a + i)] = float(0.8 * std::exp(-double(i) / 60) * std::sin(double(i) * 0.9));
    return mono(x);
}
std::vector<int> playWarp(const SampleRef& s, bool warp, bool beat, bool pitchMode = false, bool preview = false,
                          bool off = false) {
    Project p = project();
    p.tracks[0].params["attack"] = 0;
    p.tracks[0].params["decay"] = 4000;
    p.tracks[0].params["rate"] = 100;
    p.tracks[0].params[Params::rateModeId] = pitchMode ? 0 : 1;
    p.bpm = 120;
    if (warp) {
        WarpInfo w;
        w.bpm = 120;
        w.marks = {{0, 0}, {24000.0 / 96000, 1}, {52800.0 / 96000, 2}, {72000.0 / 96000, 3}};
        if (off) w.off = true;
        p.tracks[0].warps["k"] = w;
    }
    SoundSnapshot::Options o;
    o.stretchBeat = beat;
    Rig g;
    g.publish(p, {{"k", s}}, o);
    if (preview)
        g.s.warpPreview(0, SoundKey::hash("k"), 0);
    else
        g.s.noteOn(0, 48, 1, 1, 0);
    g.run(90000);
    return hitTimes(g.l);
}

void warpPutsLateHitOnBeat() {
    SampleRef s = hits();
    for (bool beat : {false, true}) {
        const std::string nm = beat ? "BEAT" : "NORMAL";
        const auto plain = playWarp(s, false, beat);
        check(plain.size() == 4 && near(plain[2], 52800, 600), "warp: без меток — как в файле " + nm);
        const auto w = playWarp(s, true, beat);
        bool ok = w.size() == 4;
        const int want[4] = {0, 24000, 48000, 72000};
        for (size_t k = 0; ok && k < 4; ++k) ok = near(w[k], want[k], 600);
        check(ok, "warp: удары на долях " + nm);
    }
    const auto offv = playWarp(s, true, false, false, false, true);
    check(offv.size() == 4 && near(offv[2], 52800, 600), "warp: выключен — как в файле");
    const auto note = playWarp(s, true, false, true);
    check(note.size() == 4 && near(note[2], 52800, 600), "warp: Rate Pitch — без варпинга");
    const auto pv = playWarp(s, true, false, true, true);
    check(pv.size() == 4 && near(pv[2], 48000, 600), "warp: ▶ WARP — по карте");
}

void pvFFT() {
    const int n = 64;
    PvFFT f(n);
    std::vector<float> src(static_cast<size_t>(n)), x(static_cast<size_t>(n)), re(static_cast<size_t>(n / 2 + 1)), im(static_cast<size_t>(n / 2 + 1));
    for (int i = 0; i < n; ++i)
        src[size_t(i)] = float(std::sin(double(i) * 0.7) + 0.3 * std::cos(double(i) * 2.1) + (i % 5 == 0 ? 0.5 : 0));
    x = src;
    f.forward(x.data(), re.data(), im.data());
    bool ok = true;
    for (int k = 0; k <= n / 2; ++k) {
        double sr = 0, si = 0;
        for (int i = 0; i < n; ++i) {
            const double a = -2 * M_PI * double(k * i) / double(n);
            sr += double(src[size_t(i)]) * std::cos(a);
            si += double(src[size_t(i)]) * std::sin(a);
        }
        ok = ok && near(re[size_t(k)], sr, 1e-3) && near(im[size_t(k)], si, 1e-3);
    }
    check(ok, "pvFFT: спектр = ДПФ");
    f.inverse(re.data(), im.data(), x.data());
    ok = true;
    for (int i = 0; i < n; ++i) ok = ok && near(x[size_t(i)] / f.gain(), src[size_t(i)], 1e-4);
    check(ok, "pvFFT: круг вперёд-назад");
}

// VocoderLoadTests.
void vocoderSpread() {
    const int n = 96000;
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / 48000;
        x[size_t(i)] = float(0.2 * std::sin(2 * M_PI * 220 * t) + 0.2 * std::sin(2 * M_PI * 329.6 * t));
    }
    for (int k = 12000; k < n; k += 12000)
        for (int i = 0; i < 480 && k + i < n; ++i)
            x[size_t(k + i)] += float(0.8 * std::exp(-double(i) / 60) * std::sin(double(i) * 0.9));
    SampleData sd(x, nullptr);
    const OnsetList& ons = sd.onsets();
    auto run = [&](int block, int voices, double speed, int blocks, std::vector<std::vector<float>>& out,
                   std::vector<int>& per) {
        Vocoder voc(4);
        voc.block = block;
        for (int v = 0; v < voices; ++v) voc.start(v, 1);
        out.assign(size_t(voices), {});
        std::vector<double> pos(size_t(voices), 0);
        for (int b = 0; b < blocks; ++b) {
            const int before = voc.framesDone();
            for (int i = 0; i < block; i += 32) {
                for (int v = 0; v < voices; ++v) {
                    voc.fill(v, x.data(), x.data(), n, false, pos[size_t(v)], speed, 1, 32, 0, 0, double(n - 1),
                             ons.at.data(), ons.count(), int64_t(b * block + i));
                    out[size_t(v)].insert(out[size_t(v)].end(), voc.outL.begin(), voc.outL.begin() + 32);
                    pos[size_t(v)] += speed * 32;
                }
            }
            per.push_back(voc.framesDone() - before);
        }
    };
    const int blocks = 300;
    std::vector<std::vector<float>> spread, plain;
    std::vector<int> nSpread, nPlain;
    run(256, 4, 0.5, blocks, spread, nSpread);
    run(512, 4, 0.5, blocks / 2, plain, nPlain);
    int plainPeak = 0, heavy = 0, sumS = 0, sumP = 0;
    for (int c : nPlain) plainPeak = std::max(plainPeak, c), sumP += c;
    for (int c : nSpread) heavy += c > 2 ? 1 : 0, sumS += c;
    check(plainPeak >= 4 && heavy < blocks / 20 && near(sumS, sumP, 8), "vocoder: кадры голосов разнесены по блокам");
    bool ok = true;
    for (size_t v = 0; v < 4; ++v) {
        double err = 0, sig = 0;
        for (size_t i = 0; i < std::min(spread[v].size(), plain[v].size()); ++i) {
            const double d = double(spread[v][i]) - plain[v][i];
            err += d * d;
            sig += double(plain[v][i]) * plain[v][i];
        }
        ok = ok && sig > 1 && 10 * std::log10(sig / std::max(1e-12, err)) > 60;
    }
    check(ok, "vocoder: звук с ранним расчётом тот же");
}
}  // namespace

int runSampler() {
    oneStepNoteAttackAndDecay();
    midiOutTrack();
    liveNoteHolds();
    pitchStartEndVolume();
    velocityLevelAndPan();
    muteGateClosesFast();
    slicesPlayMarkedPart();
    voiceLimit();
    soundChangeRetriggers();
    sequencerLockAndHold();
    stepSoundOwnSample();
    voiceSurvivesMoveAndRebind();
    synthNoteMovesWithTrack();
    previewPlaysAndStops();
    pitchModeHalfSpeed();
    pitchModeReverse();
    for (bool b : {false, true}) {
        stretchKeepsPitch(b);
        stretchFollowsTempoAndPitch(b);
        stretchReverse(b);
    }
    vocoderNearIdentity();
    vocoderKeepsHits();
    warpPutsLateHitOnBeat();
    pvFFT();
    vocoderSpread();
    std::printf("sampler: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
