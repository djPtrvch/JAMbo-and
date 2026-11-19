// Синт на дорожке через ядро: порт SynthTests (ветка swift JAMbo) — AudioCore(master: false), installSynth, события.
// testSynthKey, testSynthGrid, testDefaultsAndSetSynth, testSetSynthInitAndDecayMax — в tables_test и model_test.
#include "engine/AudioCore.h"
#include "model/Project.h"
#include "synth/Wavetable.h"
#include <algorithm>
#include <chrono>
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

std::unique_ptr<AudioCore> core(const Project& p, SoundSnapshot::Options o = {}) {
    auto c = std::make_unique<AudioCore>(false);
    c->installSynth();
    c->publish(p, {}, o);
    return c;
}
/// Дорожка 1 — синт e, громкость 0 dB.
Project synthProject(int e, const ParamMap& params = {}) {
    Project p;
    p.tracks[0].setSynth(e);
    p.tracks[0].vol = 0;
    for (const auto& [k, v] : params) p.tracks[0].params[k] = v;
    return p;
}
std::vector<float> render(AudioCore& c, int frames) {
    std::vector<float> l(static_cast<size_t>(frames)), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        c.render(n, l.data() + pos, br.data());
        pos += n;
    }
    return l;
}
float rms(const std::vector<float>& x, int center, int half = 240) {
    float s = 0;
    for (int i = center - half; i < center + half; ++i) s += x[size_t(i)] * x[size_t(i)];
    return std::sqrt(s / float(2 * half));
}
float peak(const std::vector<float>& x, int from, int to) {
    float m = 0;
    for (int i = from; i < to; ++i) m = std::max(m, std::fabs(x[size_t(i)]));
    return m;
}
bool finite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}
const SynthVoices& voices(const AudioCore& c) { return *c.sampler.synth; }

void attackAndRelease() {
    auto c = core(synthProject(17, {{"attack", 200}, {"decay", 400}}));
    c->events.push(CoreEvent::noteOn(0, 60, 1, 1));
    auto x = render(*c, 24000);
    const float held = rms(x, 20000);
    check(held > 0.05f, "core synth: держится");
    check(std::fabs(rms(x, 4800) / held - 0.5f) < 0.06f, "core synth: атака 200 мс — на 100 мс половина");
    c->events.push(CoreEvent::noteOff(0, 1));
    x = render(*c, 48000);
    check(std::fabs(rms(x, 4800) / held - float(std::exp(-1.0))) < 0.05f, "core synth: Decay как Release, τ 100 мс");
    check(voices(*c).activeCount() == 0, "core synth: после хвоста голос замолчал");
}

void seventhNoteSteals() {
    auto c = core(synthProject(17));
    for (int k = 0; k < 7; ++k) c->events.push(CoreEvent::noteOn(0, 48 + k, k + 1, 1));
    render(*c, 4800);
    const auto& s = voices(*c);
    std::vector<int> notes;
    for (int v = 0; v < SynthVoices::perTrack; ++v)
        if (s.isActive(v)) notes.push_back(s.voiceNote(v));
    std::sort(notes.begin(), notes.end());
    check(s.activeCount() == 6 && notes == std::vector<int>{49, 50, 51, 52, 53, 54},
          "core synth: первую ноту украла седьмая");
}

void stepSound() {
    // Дорожка без звука, у шага — свой звук «mi:18» (FM).
    Project p;
    p.tracks[0].vol = 0;
    p.tracks[0].params["decay"] = 100;
    auto& st = p.tracks[0].pagesMut()[0][0];
    st.on = true;
    st.notes = {60};
    st.sound = "mi:18";
    p.tracks[0].setLength(64);
    auto c = core(p);
    c->events.push(CoreEvent::play(true));
    const auto x = render(*c, 14000);
    check(rms(x, 1200) > 0.01f, "core synth: свой звук шага — синт");
    check(voices(*c).activeCount() == 0 && voices(*c).voiceEngine(0) == 18,
          "core synth: нота в 1 шаг — тишина после хвоста, движок шага");
}

void engineChange() {
    Project p = synthProject(17);
    auto c = core(p);
    c->events.push(CoreEvent::noteOn(0, 57, 3, 1));
    render(*c, 4800);
    p.tracks[0].setSynth(18);
    c->publish(p);
    const auto x = render(*c, 4800);
    const auto& s = voices(*c);
    std::vector<int> engines;
    for (int v = 0; v < SynthVoices::perTrack; ++v)
        if (s.isActive(v)) engines.push_back(s.voiceEngine(v));
    check(engines == std::vector<int>{18} && rms(x, 4000) > 0.01f,
          "core synth: держащаяся нота — новым движком");
}

void muteSilences() {
    Project p = synthProject(18);
    auto c = core(p);
    c->events.push(CoreEvent::noteOn(0, 60, 1, 1));
    render(*c, 4800);
    p.tracks[0].mute = true;
    c->publish(p);
    check(rms(render(*c, 4800), 4000) < 1e-4f, "core synth: mute глушит");
}

/// Нагрузка: 8 дорожек × 6 голосов (живые ноты), 1 с звука; печатает долю реального времени.
void load48() {
    Project p;
    for (int t = 0; t < 8; ++t) p.tracks[size_t(t)].setSynth(t % 2 == 0 ? 17 : 18);
    auto c = core(p);
    for (int t = 0; t < 8; ++t)
        for (int k = 0; k < 6; ++k) c->events.push(CoreEvent::noteOn(t, 48 + k * 3, t * 10 + k, 1));
    const auto t0 = std::chrono::steady_clock::now();
    const auto x = render(*c, 48000);
    const double load = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("core synth load: 48 voices — %.1f%% of one core\n", load * 100);
    check(voices(*c).activeCount() == 48 && finite(x), "core synth: 48 голосов, без NaN");
}

/// ANALOG, синус: A4 = 440 Гц с точностью до дрейфа (±3 цента).
void analogA4() {
    auto c = core(synthProject(17));
    c->events.push(CoreEvent::noteOn(0, 69, 1, 1));
    const auto x = render(*c, 48000);
    double first = -1, last = -1;
    int count = 0;
    for (int i = 12000; i < 47999; ++i)
        if (x[size_t(i)] <= 0 && x[size_t(i + 1)] > 0) {
            const double t = double(i) + double(-x[size_t(i)] / (x[size_t(i + 1)] - x[size_t(i)]));
            if (first < 0) first = t;
            last = t;
            ++count;
        }
    const double hz = double(count - 1) / ((last - first) / 48000);
    check(std::fabs(hz - 440) < 440 * 0.0018, "core synth: ANALOG A4 = 440 Гц");
}

/// KICK монофонична: аккорд — один голос; ANALOG и FM звучат и без NaN.
void jamboEngines() {
    auto c = core(synthProject(16));
    const int chord[] = {36, 40, 43};
    for (int k = 0; k < 3; ++k) c->events.push(CoreEvent::noteOn(0, chord[k], k + 1, 1));
    const auto x = render(*c, 4800);
    check(voices(*c).activeCount() == 1 && peak(x, 0, 4800) > 0.05f, "core synth: KICK монофонична и звучит");
    for (int e : {17, 18}) {
        auto d = core(synthProject(e));
        d->events.push(CoreEvent::noteOn(0, 48, 1, 1));
        const auto y = render(*d, 9600);
        check(finite(y) && peak(y, 4800, 9600) > 0.05f, "core synth: движок " + std::to_string(e) + " звучит");
    }
}

/// WTABLE: без готовой таблицы молчит, с BASIC звучит; таблица — у дорожки.
void wavetable() {
    Project p = synthProject(19);
    check(p.tracks[0].table.empty(), "core wt: таблица дорожки — BASIC");
    auto silent = core(p);
    silent->events.push(CoreEvent::noteOn(0, 48, 1, 1));
    check(peak(render(*silent, 9600), 0, 9600) < 1e-6f, "core wt: без таблицы молчит");
    SoundSnapshot::Options o;
    o.tables[""] = std::shared_ptr<WtTable>(WtTable::builtin("BASIC"));
    auto c = core(p, o);
    c->events.push(CoreEvent::noteOn(0, 48, 1, 1));
    const auto x = render(*c, 9600);
    check(finite(x) && peak(x, 4800, 9600) > 0.05f, "core wt: с BASIC звучит");
    p.tracks[0].table = WavetableKey::builtin("METAL");
    check(p.tracks[0].table == "builtin:METAL" && WavetableKey::name(p.tracks[0].table) == "METAL" &&
              WavetableKey::name("") == "BASIC" && WavetableKey::builtin("BASIC").empty(),
          "WavetableKey");
}
}  // namespace

int runSynthCoreTests() {
    attackAndRelease();
    seventhNoteSteals();
    stepSound();
    engineChange();
    muteSilences();
    load48();
    analogA4();
    jamboEngines();
    wavetable();
    std::printf("synth core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
