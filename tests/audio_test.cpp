// Аудиоядро: очередь, модуляция, миниатюра ENV, снимок звука (порт QueueTests, ModTests без AudioCore, EnvDotTests;
// ветка swift JAMbo). Тесты с AudioCore — после его переноса.
#include "mod/ModEngine.h"
#include "model/Project.h"
#include "sampler/SoundSnapshot.h"
#include "util/SPSCQueue.h"
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

using namespace jambo;

namespace {
int fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}
bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }

void queue() {
    SPSCQueue<int> q(5, 0);  // → 8
    bool ok = true;
    for (int i = 0; i < 8; ++i) ok = ok && q.push(i);
    check(ok && !q.push(99), "queue: полная очередь не принимает");
    for (int i = 0; i < 8; ++i) ok = ok && q.pop() == i;
    check(ok && !q.pop(), "queue: порядок");
    SPSCQueue<int> w(4, 0);
    for (int r = 0; r < 100; ++r) {
        ok = ok && w.push(r) && w.push(-r);
        ok = ok && w.pop() == r && w.pop() == -r;
    }
    check(ok && w.count() == 0, "queue: по кругу");
    SPSCQueue<int> t(64, 0);
    const int n = 200000;
    std::thread wr([&] {
        int i = 0;
        while (i < n) if (t.push(i)) ++i;
    });
    int next = 0;
    while (next < n) if (auto v = t.pop()) { ok = ok && *v == next; ++next; }
    wr.join();
    check(ok, "queue: два потока");
    AtomicFloat a(0.5f);
    a.store(-3.25f);
    check(a.load() == -3.25f, "AtomicFloat");
}

void shapes() {
    check(near(ModEngine::shape(0, 0.25, 0), 1, 1e-12) && near(ModEngine::shape(1, 0.5, 0), 0, 1e-12) &&
              near(ModEngine::shape(1, 0.25, 0), 1, 1e-12) && near(ModEngine::shape(2, 0, 0), -1, 1e-12) &&
              near(ModEngine::shape(3, 0.6, 0), -1, 1e-12) && near(ModEngine::shape(4, 0.3, 0.42), 0.42, 1e-12),
          "mod: формы LFO");
}

/// ADSR по умолчанию: атака 10 мс, Decay 100 мс до Sustain 40 %, Release 300 мс.
void envelope() {
    ModEngine m(48000, 1);
    m.trig(0, 1000, 48000);
    check(near(m.env(0, 1000), 0), "env: начало");
    check(near(m.env(0, 1240), 0.5), "env: середина атаки");
    check(near(m.env(0, 1000 + 480 + 2400), 0.7), "env: середина Decay");
    check(near(m.env(0, 1000 + 20000), 0.4), "env: Sustain");
    check(near(m.env(0, 49000 + 7200), 0.2), "env: середина Release");
    check(near(m.env(0, 49000 + 20000), 0), "env: конец");
}

/// LFO1 Square 1 Гц на Level со степенью −1: значение по ходу ручки.
void lfoValue() {
    Project p;
    p.tracks[0].params["lfo1.wave"] = 3;
    p.tracks[0].params["lfo1.speed"] = 1;
    p.tracks[0].assigns["level"] = ModAssign(1, -1);
    SoundSnapshot snap(p, {}, SoundSnapshot::Options());
    ModEngine m(48000, 1);
    std::vector<uint16_t> mask(16, 0);
    std::vector<float> vals(16 * Params::modCount, 0);
    m.begin(snap.data(), 256, mask.data(), vals.data());
    check(m.modulated(0) && !m.modulated(1), "lfo: назначение есть только у дорожки 1");
    check(near(m.value(0, VoiceParam::level, 127, 0), 0, 1e-6), "lfo: +1 · −1 — Level 0");
    for (int k = 0; k < 120; ++k) m.begin(snap.data(), 256, mask.data(), vals.data());  // 0,64 с — вторая половина
    check(near(m.value(0, VoiceParam::level, 0, 0), 127, 1e-6), "lfo: −1 · −1 — Level 100 %");
}

void envDot() {
    const double cases[][4] = {{199, 474, 0, 42}, {10, 100, 0.4, 300}, {500, 50, 1, 200}, {2, 800, 0.2, 1000}, {50, 0, 0.6, 100}};
    bool ok = true;
    for (const auto& c : cases) {
        const double a = c[0], d = c[1], s = c[2], r = c[3];
        const double gate = a + d + std::max(200.0, (a + d + r) * 0.3);
        for (double g : {20.0, a * 0.5, a, a + d * 0.3, a + d + 50, 5000.0})
            for (double t = 0; t < g + r + 50; t += 7) {
                const double x = EnvDot::x(a, d, s, r, t, g, gate);
                const double on = EnvDot::curve(a, d, s, r, x, gate);
                ok = ok && x >= 0 && x <= gate + r + 1e-9;
                if (t >= g || t <= gate) ok = ok && near(on, EnvDot::curve(a, d, s, r, t, g));
            }
    }
    check(ok, "envDot: точка на кривой");
    const double a = 199, d = 474, s = 0, r = 42, gate = a + d + 200;
    for (double g : {100.0, 300.0, 700.0}) {
        double last = -1;
        for (double t = g; t < g + r + 10; t += 1) {
            const double x = EnvDot::x(a, d, s, r, t, g, gate);
            ok = ok && x >= last - 1e-9;
            last = x;
        }
    }
    check(ok, "envDot: только вперёд");
}

void soundSnapshot() {
    Project p;
    p.tracks[0].sample = "kick";
    p.tracks[0].params["filter"] = 40;
    p.tracks[0].slices["kick"] = {SliceMark(0), SliceMark(0.5, 62), SliceMark(0.7)};
    p.tracks[1].mute = true;
    std::map<std::string, SampleRef> samples;
    samples["kick"] = std::make_shared<SampleData>(std::vector<float>(100, 0.5f), nullptr);
    SoundSnapshot snap(p, samples, SoundSnapshot::Options());
    const SoundData& d = snap.data();
    check(d.trackCount == 8 && d.param(0, VoiceParam::filter) == 40, "snapshot: ручки");
    check(d.open[0] == 1 && d.open[1] == 0, "snapshot: mute");
    check(near(d.gain[0], Params::volGain(Params::volDef), 1e-6), "snapshot: громкость");
    const int e = d.entry(0, SoundKey::hash("kick"));
    check(e == 0 && d.eFrames[0] == 100 && d.entry(1, SoundKey::hash("kick")) < 0, "snapshot: сэмпл дорожки");
    // CHROMATIC от C3 (48): 48, 62 (своя — номер по порядку тоже занимает), 50.
    const int a = d.eSliceStart[size_t(e)];
    check(d.eSliceCount[size_t(e)] == 3 && d.sliceNote[size_t(a)] == 48 && d.sliceNote[size_t(a + 1)] == 62 &&
              d.sliceNote[size_t(a + 2)] == 50,
          "snapshot: ноты нарезки");
    check(SoundKey::hash("") == 0 && SoundKey::hash("a") == 0xaf63dc4c8601ec8cULL, "SoundKey: FNV-1a");
}
// TempoDetectTests: синтетические ритмы.
uint32_t seed = 1;
float noise() {
    seed = seed * 1664525u + 1013904223u;
    return float(int32_t(seed)) / float(INT32_MAX);
}
SampleRef groove(double bpm, double sec = 60, bool fourOnFloor = true) {
    const double rate = 48000;
    const int n = int(sec * rate);
    std::vector<float> x(size_t(n), 0);
    const double beat = 60 / bpm * rate;
    for (int b = 0; double(b) * beat < double(n); ++b) {
        const int at = int(double(b) * beat);
        if (fourOnFloor || b % 4 == 0 || b % 4 == 2)
            for (int i = 0; i < 6000 && at + i < n; ++i) {
                const double t = double(i) / rate;
                x[size_t(at + i)] += float(std::sin(2 * M_PI * 55 * t) * std::exp(-t * 18)) * 0.8f;
            }
        if (b % 2 == 1)
            for (int i = 0; i < 4000 && at + i < n; ++i) x[size_t(at + i)] += noise() * float(std::exp(-double(i) / 900)) * 0.4f;
        const int off = at + int(beat / 2);
        for (int i = 0; i < 1200 && off + i < n; ++i) x[size_t(off + i)] += noise() * float(std::exp(-double(i) / 200)) * 0.15f;
    }
    for (auto& v : x) v += noise() * 0.005f;
    return std::make_shared<SampleData>(x, nullptr);
}

void tempoDetect() {
    seed = 1;
    check(TempoDetect::bpm(*groove(126)) == 126.0 && TempoDetect::bpm(*groove(128)) == 128.0, "tempo: house");
    check(TempoDetect::bpm(*groove(90, 60, false)) == 90.0 && TempoDetect::bpm(*groove(100)) == 100.0 &&
              TempoDetect::bpm(*groove(140)) == 140.0 && TempoDetect::bpm(*groove(174, 60, false)) == 174.0 &&
              TempoDetect::bpm(*groove(85)) == 85.0,
          "tempo: диапазон");
    check(std::fabs(TempoDetect::bpm(*groove(123.5)).value_or(0) - 123.5) <= 0.11, "tempo: дробный");
    check(!TempoDetect::bpm(SampleData(std::vector<float>(48000 * 20, 0.f), nullptr)) &&
              !TempoDetect::bpm(*groove(120, 3)),
          "tempo: тишина и коротко");
}

void warpMap() {
    WarpInfo info;
    info.marks = {WarpMark{26400.0 / 96000, 1}, WarpMark{0.5, 2}};
    const WarpMap map(info, 96000, 120);
    check(near(map.beat(26400), 1) && near(map.beat(37200), 1.5) && near(map.beat(72000), 3) && near(map.beat(2400), 0),
          "warp: карта");
    bool ok = true;
    for (double b = -0.5; b <= 4; b += 0.25) ok = ok && near(map.beat(map.position(b)), b);
    check(ok, "warp: туда и обратно");
    const WarpMap none(WarpInfo(), 96000, 120);
    WarpInfo own;
    own.bpm = 98;
    check(near(none.beat(36000), 1.5) && WarpMap::bpm(nullptr, 0) == 120 && WarpMap::bpm(&own, 125) == 98,
          "warp: без меток, BPM файла");
    const auto m = WarpMap::autoMarks({10, 6500, 11000, 12100, 30500}, 48000, 120, 0.25);
    check(m.size() == 4 && m[0].beat == 0 && m[1].beat == 0.25 && m[2].beat == 0.5 && m[3].beat == 1.25 &&
              m[0].at == 0 && near(m[1].at * 48000, 6500, 1e-6),
          "warp: AUTO WARP по сетке");
    WarpInfo marked;
    marked.bpm = 98;
    marked.marks = {WarpMark{0, 0}};
    WarpInfo off = marked;
    off.off = true;
    check(WarpMap::bpm(&marked, 125, 140.0) == 140 && WarpMap::bpm(&marked, 125) == 98 &&
              WarpMap::bpm(&own, 125, 140.0) == 98 && WarpMap::bpm(&off, 125, 140.0) == 98,
          "warp: WARP TO PROJECT BPM");
    // Снимок: темп файла сэмпла с метками — темп проекта (follow) или свой.
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].params[Params::rateModeId] = 1;
    p.bpm = 133;
    WarpInfo wi;
    wi.bpm = 120;
    wi.marks = {WarpMark{0, 0}, WarpMark{0.5, 2}};
    p.tracks[0].warps["k"] = wi;
    std::map<std::string, SampleRef> samples;
    samples["k"] = groove(120, 4);
    for (bool fol : {true, false}) {
        SoundSnapshot::Options o;
        o.warpFollow = fol;
        SoundSnapshot snap(p, samples, o);
        const int e = snap.data().entryStart[0];
        check(snap.data().eBpm[size_t(e)] == (fol ? 133.0 : 120.0) && snap.data().eWarpCount[size_t(e)] == 2,
              "warp: снимок звука, follow " + std::to_string(fol));
    }
}
}  // namespace

int runAudio() {
    fails = 0;
    queue();
    shapes();
    envelope();
    lfoValue();
    envDot();
    soundSnapshot();
    tempoDetect();
    warpMap();
    std::printf("audio: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
