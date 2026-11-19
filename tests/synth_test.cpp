// Голоса синта: порт SynthTests (ветка swift JAMbo) напрямую через SynthVoices — без AudioCore (микшер, мастер), выход
// — шина дорожки 0. Уровни сравниваются отношениями (громкость микшера не участвует).
#include "model/Project.h"
#include "sampler/SoundSnapshot.h"
#include "synth/SynthVoices.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace jambo;

namespace {
int fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}

struct Rig {
    static constexpr int block = 256;
    std::unique_ptr<SoundSnapshot> snap;
    SynthVoices voices{48000};
    std::vector<float> busL, busR, sendL, sendR;
    std::vector<uint8_t> used;
    int64_t frame = 0;

    explicit Rig(const Project& p) {
        snap = std::make_unique<SoundSnapshot>(p, std::map<std::string, SampleRef>(), SoundSnapshot::Options());
        busL.assign(size_t(16 * block), 0);
        busR = busL;
        sendL.assign(size_t(16 * 3 * block), 0);
        sendR = sendL;
        used.assign(16, 0);
    }
    void publish(const Project& p, SoundSnapshot::Options o = {}) {
        auto s = std::make_unique<SoundSnapshot>(p, std::map<std::string, SampleRef>(), o);
        voices.adopt(s->data(), frame);
        snap = std::move(s);
    }
    void noteOn(int t, int note, int id) {
        const int16_t n = int16_t(note);
        const SoundData& d = snap->data();
        const int e = std::stoi(p0sound(t).substr(3));
        voices.trigger(d, t, e, frame, &n, 1, 0, nullptr, SynthVoices::forever, false, id, false);
    }
    std::string p0sound(int t) const { return sounds[size_t(t)]; }
    std::vector<std::string> sounds = std::vector<std::string>(16);
    /// Дорожка 0, левый канал.
    std::vector<float> render(int frames, int track = 0) {
        std::vector<float> out;
        while (int(out.size()) < frames) {
            const int n = std::min(block, frames - int(out.size()));
            std::fill(used.begin(), used.end(), 0);
            voices.render(snap->data(), frame, n, busL.data(), busR.data(), sendL.data(), sendR.data(), used.data(), block);
            for (int i = 0; i < n; ++i) out.push_back(used[size_t(track)] ? busL[size_t(track * block + i)] : 0.f);
            frame += n;
        }
        return out;
    }
};

Project synthProject(int engine, std::map<std::string, double> params = {}) {
    Project p;
    p.tracks[0].setSynth(engine);
    for (const auto& [k, v] : params) p.tracks[0].params[k] = v;
    return p;
}

Rig* makeRig(const Project& p) {
    auto* r = new Rig(p);
    for (int t = 0; t < int(p.tracks.size()); ++t) r->sounds[size_t(t)] = p.tracks[size_t(t)].sample;
    return r;
}

float rms(const std::vector<float>& x, int c, int half = 240) {
    double s = 0;
    for (int i = c - half; i < c + half; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    return float(std::sqrt(s / (2 * half)));
}
float peak(const std::vector<float>& x, int a, int b) {
    float m = 0;
    for (int i = a; i < b; ++i) m = std::max(m, std::fabs(x[size_t(i)]));
    return m;
}
bool finite(const std::vector<float>& x) {
    for (float v : x) if (!std::isfinite(v)) return false;
    return true;
}

void attackAndRelease() {
    std::unique_ptr<Rig> c(makeRig(synthProject(17, {{"attack", 200}, {"decay", 400}})));
    c->noteOn(0, 60, 1);
    auto x = c->render(24000);
    const float held = rms(x, 20000);
    check(held > 0.05f, "synth: держится");
    check(std::fabs(rms(x, 4800) / held - 0.5f) < 0.06f, "synth: атака 200 мс — на 100 мс половина");
    c->voices.release(0, 1, c->frame);
    x = c->render(48000);
    check(std::fabs(rms(x, 4800) / held - float(std::exp(-1.0))) < 0.05f, "synth: Decay как Release, τ 100 мс");
    check(c->voices.activeCount() == 0, "synth: после хвоста голос замолчал");
}

void seventhNoteSteals() {
    std::unique_ptr<Rig> c(makeRig(synthProject(17)));
    for (int k = 0; k < 7; ++k) c->noteOn(0, 48 + k, k + 1);
    c->render(4800);
    check(c->voices.activeCount() == 6, "synth: 6 голосов на дорожку");
}

void muteByAdopt() {
    Project p = synthProject(18);
    std::unique_ptr<Rig> c(makeRig(p));
    c->noteOn(0, 60, 1);
    c->render(4800);
    // Перестановка дорожек (другой id) — голоса гаснут.
    p.tracks[0].id = 99;
    c->publish(p);
    auto x = c->render(4800);
    check(rms(x, 4000) < 1e-4f && c->voices.activeCount() == 0, "synth: другой id дорожки — голос погас");
}

void load48() {
    Project p;
    for (int t = 0; t < 8; ++t) p.tracks[size_t(t)].setSynth(t % 2 == 0 ? 17 : 18);
    std::unique_ptr<Rig> c(makeRig(p));
    for (int t = 0; t < 8; ++t)
        for (int k = 0; k < 6; ++k) c->noteOn(t, 48 + k * 3, t * 10 + k);
    const auto t0 = std::chrono::steady_clock::now();
    auto x = c->render(48000);
    const double load = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("synth load: 48 voices — %.1f%% of one core\n", load * 100);
    check(c->voices.activeCount() == 48 && finite(x), "synth: 48 голосов, без NaN");
}

/// ANALOG, синус: A4 = 440 Гц с точностью до дрейфа (±3 цента).
void analogA4() {
    std::unique_ptr<Rig> c(makeRig(synthProject(17)));
    c->noteOn(0, 69, 1);
    auto x = c->render(48000);
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
    check(std::fabs(hz - 440) < 440 * 0.0018, "synth: ANALOG A4 = 440 Гц");
}

void jamboEnginesSound() {
    std::unique_ptr<Rig> c(makeRig(synthProject(16)));
    for (int k = 0; k < 3; ++k) c->noteOn(0, 36 + k * 4, k + 1);
    auto x = c->render(4800);
    check(c->voices.activeCount() == 1 && peak(x, 0, 4800) > 0.05f, "synth: KICK монофонична и звучит");
    for (int e : {17, 18}) {
        std::unique_ptr<Rig> r(makeRig(synthProject(e)));
        r->noteOn(0, 48, 1);
        auto y = r->render(9600);
        check(finite(y) && peak(y, 4800, 9600) > 0.05f, "synth: движок " + std::to_string(e) + " звучит");
    }
}

void wavetable() {
    Project p = synthProject(19);
    std::unique_ptr<Rig> s(makeRig(p));
    s->noteOn(0, 48, 1);
    check(peak(s->render(9600), 0, 9600) < 1e-6f, "synth: WTABLE без таблицы молчит");
    std::unique_ptr<Rig> c(makeRig(p));
    SoundSnapshot::Options o;
    o.tables[""] = std::shared_ptr<WtTable>(WtTable::builtin("BASIC"));
    c->publish(p, o);
    c->noteOn(0, 48, 1);
    auto x = c->render(9600);
    check(finite(x) && peak(x, 4800, 9600) > 0.05f, "synth: WTABLE с BASIC звучит");
}
}  // namespace

int runSynth() {
    fails = 0;
    attackAndRelease();
    seventhNoteSteals();
    muteByAdopt();
    load48();
    analogA4();
    jamboEnginesSound();
    wavetable();
    std::printf("synth: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
