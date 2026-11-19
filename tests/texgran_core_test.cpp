// TEXTURE и GRANULAR в голосе синта через ядро: порт TexGranVoiceTests (ветка swift JAMbo) — без замеров нагрузки
// (testCost, testGranularCost: только печать) и иконок (EngineIcons не перенесены); плюс
// CaptureTests.testRecordedSampleOnTrack (правки папки дорожки).
#include "engine/AudioCore.h"
#include "grain_sources.h"
#include "master/Limiter.h"
#include "model/Json.h"
#include "model/Project.h"
#include "model/SynthEngines.h"
#include "synth/Jambo.h"
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

using Samples = std::map<std::string, SampleRef>;

std::unique_ptr<AudioCore> core(int e, const ParamMap& params = {}) {
    Project p;
    p.tracks[0].setSynth(e);
    p.tracks[0].vol = 0;
    for (const auto& [k, v] : params) p.tracks[0].params[k] = v;
    auto c = std::make_unique<AudioCore>(false);
    c->installSynth();
    c->publish(p);
    return c;
}

std::unique_ptr<AudioCore> coreWithSample(const std::string& grain, const Samples& samples) {
    Project p;
    p.tracks[0].setSynth(J::granular);
    p.tracks[0].vol = 0;
    p.tracks[0].grain = grain;
    auto c = std::make_unique<AudioCore>(false);
    c->installSynth();
    c->publish(p, samples);
    return c;
}

struct LR {
    std::vector<float> l, r;
};
LR render(AudioCore& c, int frames) {
    LR o{std::vector<float>(static_cast<size_t>(frames)), std::vector<float>(static_cast<size_t>(frames))};
    std::vector<float> bl(256), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        c.render(n, bl.data(), br.data());
        std::copy(bl.begin(), bl.begin() + n, o.l.begin() + pos);
        std::copy(br.begin(), br.begin() + n, o.r.begin() + pos);
        pos += n;
    }
    return o;
}
double rms(const std::vector<float>& x) {
    double s = 0;
    for (float v : x) s += double(v) * v;
    return std::sqrt(s / double(std::max<size_t>(1, x.size())));
}
bool finite(const LR& o) {
    for (float v : o.l) if (!std::isfinite(v)) return false;
    for (float v : o.r) if (!std::isfinite(v)) return false;
    return true;
}
int active(const AudioCore& c) { return c.sampler.synth->activeCount(); }

void table() {
    const auto& g = Params::grid("mi:25");
    const auto& h = Params::grid("mi:26");
    check(SynthEngines::engine(25)->name == "TEXTURE" && SynthEngines::engine(26)->name == "GRANULAR" && J::mono(25) &&
              !J::mono(26) && J::stereo(25) && J::stereo(26) && !J::stereo(24),
          "texgran: движки, моно, стерео");
    check(g[0].label == "Color" && g[0].format(40) == "white" && g[1].label == "Grit" && g[1].format(50) == "50%" &&
              g[3].label == "Metal" && g[2].format(30) == "30%" && h[1].label == "Size" && h[1].format(0) == "10ms" &&
              h[1].format(100) == "800ms" && h[2].format(100) == "×24.0",
          "texgran: ручки");
    Track t = Project().tracks[0];
    t.setSynth(25);
    bool ok = t.params.at("harm") == 40 && t.params.at("timbre") == 30 && t.params.at("p4") == 0;
    t.setSynth(26);
    check(ok && t.params.at("harm") == 8 && t.params.at("timbre") == 62 && t.params.at("morph") == 60 &&
              t.params.at("p4") == 25,
          "texgran: значения при выборе");
}

void texture() {
    {
        auto c = core(J::texture);
        const int notes[3] = {57, 60, 64};
        for (int k = 0; k < 3; ++k) c->events.push(CoreEvent::noteOn(0, notes[k], k + 1, 1));
        const auto o = render(*c, 48000);
        double diff = 0;
        for (size_t i = 0; i < o.l.size(); ++i) diff += std::fabs(double(o.l[i] - o.r[i]));
        check(active(*c) == 1 && finite(o) && rms(o.l) > 0.005 && diff > 10, "texture: аккорд — один голос, стерео");
    }
    {
        auto c = core(J::texture, {{"pan", -64}});
        c->events.push(CoreEvent::noteOn(0, 57, 1, 1));
        const auto o = render(*c, 24000);
        check(rms(o.l) > 0.005 && rms(o.r) < 1e-6, "texture: Pan влево — правый молчит");
    }
    {
        auto c = core(J::texture, {{"attack", 2}, {"decay", 200}});
        c->events.push(CoreEvent::noteOn(0, 57, 1, 1));
        render(*c, 24000);
        c->events.push(CoreEvent::noteOff(0, 1));
        render(*c, 48000);
        check(active(*c) == 0, "texture: после отпускания дозвучал и замолчал");
    }
}

void granular() {
    const auto demo = grain_sources::demo();
    {
        auto c = coreWithSample("demo", {{"demo", demo}});
        c->events.push(CoreEvent::noteOn(0, 48, 1, 1));
        const auto o = render(*c, 48000);
        auto q = coreWithSample("", {{"demo", demo}});
        q->events.push(CoreEvent::noteOn(0, 48, 1, 1));
        const auto o2 = render(*q, 24000);
        auto m = coreWithSample("missing", {});
        m->events.push(CoreEvent::noteOn(0, 48, 1, 1));
        const auto o3 = render(*m, 24000);
        check(finite(o) && rms(o.l) > 0.005 && rms(o.r) > 0.005 && rms(o2.l) < 1e-9 && rms(o3.l) < 1e-9,
              "granular: свой сэмпл звучит, без него — тишина");
    }
    {
        auto c = coreWithSample("pad", {{"pad", grain_sources::pad()}});
        const int notes[3] = {48, 51, 55};
        for (int k = 0; k < 3; ++k) c->events.push(CoreEvent::noteOn(0, notes[k], k + 1, 1));
        const auto o = render(*c, 48000);
        const bool poly = active(*c) == 3 && finite(o) && rms(o.l) > 0.005;
        for (int k = 1; k <= 3; ++k) c->events.push(CoreEvent::noteOff(0, k));
        render(*c, 48000 * 8);
        check(poly && active(*c) == 0, "granular: голос на ноту, после отпускания дозвучали");
    }
}

struct Level {
    double rms, peak, gr;
};
Level masterRun(const std::vector<double>* k, const std::vector<int>& notes) {
    Project p;
    p.tracks[0].setSynth(J::granular);
    p.tracks[0].grain = "pad";
    if (k)
        for (size_t j = 0; j < SynthEngines::knobIds.size(); ++j) p.tracks[0].params[SynthEngines::knobIds[j]] = (*k)[j];
    AudioCore c(true);
    c.installSynth();
    c.publish(p, {{"pad", grain_sources::pad()}});
    for (size_t i = 0; i < notes.size(); ++i) c.events.push(CoreEvent::noteOn(0, notes[i], int(i) + 1, 1));
    render(c, 48000);
    std::vector<float> l, r;
    double gr = 0;
    for (int b = 0; b < 48000 * 4 / 256; ++b) {
        const auto o = render(c, 256);
        l.insert(l.end(), o.l.begin(), o.l.end());
        r.insert(r.end(), o.r.begin(), o.r.end());
        gr = std::max(gr, c.master.lim->frameGr);
    }
    float pk = 0;
    for (float v : l) pk = std::max(pk, std::fabs(v));
    for (float v : r) pk = std::max(pk, std::fabs(v));
    return {(rms(l) + rms(r)) / 2, pk, gr};
}

void masterLevel() {
    const auto d = masterRun(nullptr, {57});
    const std::vector<double> k{8, 100, 100, 25};
    const auto dense = masterRun(&k, {57, 60, 62, 64, 67, 69});
    std::printf("GRANULAR master: default RMS %.3f (%.1f dBFS), peak %.2f, lim GR %.2f dB; dense x6 RMS %.3f, peak %.3f, "
                "lim GR %.2f dB\n",
                d.rms, 20 * std::log10(d.rms), d.peak, d.gr, dense.rms, dense.peak, dense.gr);
    check(std::fabs(20 * std::log10(d.rms / 0.1)) < 3 && dense.gr < 0.01, "granular: уровень на мастере, лимитер не упирается");
}

void storage() {
    Project p;
    p.tracks[0].setSynth(J::granular);
    p.tracks[0].grain = "abc";
    Project back;
    const bool round = json::get(json::put(p), back) && back.tracks[0].grain == "abc";
    const auto pr = p.tracks[0].preset("G");
    Track t = Project().tracks[1];
    if (pr) t.applyItem(FolderItem::ofPreset(*pr));
    const auto* e = SynthEngines::engine(SynthKey::engine(t.sample));
    const TrackSound snd = p.tracks[0].sound();
    TrackSound sndBack;
    const bool sndRound = json::get(json::put(snd), sndBack) && sndBack == snd;
    check(p.sampleKeys().count("abc") && round && pr && pr->grain == std::string("abc") && t.grain == "abc" && e &&
              e->name == "GRANULAR" && snd.grain == std::string("abc") && sndRound,
          "granular: сэмпл — в проекте, пресете и звуке");
    const TrackSound plain = Project().tracks[0].sound();
    const std::string old = json::put(plain).dump();
    TrackSound plainBack;
    Track a = Project().tracks[0];
    a.setSynth(J::texture);
    const auto ta = a.preset("T");
    check(!plain.grain && old.find("grain") == std::string::npos && json::get(Json::parse(old), plainBack) &&
              plainBack == plain && ta && !ta->grain,
          "granular: без сэмпла — поля нет");
}

/// CaptureTests.testRecordedSampleOnTrack: запись — на место сэмпла (и в папке) или в папку.
void recordedOnTrack() {
    Track t = Project().tracks[0];
    t.addSamples({"a", "b"});
    t.applyItem(t.folder.items[1]);
    t.folder.index = 1;
    t.addRecordedSample("r1");
    bool ok = t.sample == "r1" && t.folder.sampleKeys() == std::vector<std::string>{"a", "r1"} && t.folder.index == 1;
    t.addRecordedSample("r2");
    ok = ok && t.folder.sampleKeys() == std::vector<std::string>{"a", "r2"};
    Track s = Project().tracks[0];
    s.sample = SynthKey::key(0);
    s.addRecordedSample("r");
    ok = ok && s.sample == "r" && s.folder.sampleKeys() == std::vector<std::string>{"r"};
    Track e = Project().tracks[0];
    e.addRecordedSample("r");
    check(ok && e.sample == "r" && e.folder.index == 0, "capture: запись на дорожку");
}
}  // namespace

int runTexGranCoreTests() {
    table();
    texture();
    granular();
    masterLevel();
    storage();
    recordedOnTrack();
    std::printf("texgran core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
