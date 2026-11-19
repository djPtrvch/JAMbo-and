// Секвенсор: порт SequencerTests (ветка swift JAMbo) без AudioCore — снимок паттернов, Sequencer.process, сообщения.
#include "model/Project.h"
#include "sequencer/SeqSnapshot.h"
#include "sequencer/Sequencer.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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
    std::unique_ptr<SeqSnapshot> snap;
    Sequencer seq{48000, 120};
    int64_t frame = 0;
    std::vector<float> audio;
    std::vector<SeqOut> outs;
    explicit Rig(const Project& p) : snap(std::make_unique<SeqSnapshot>(p)) { seq.setData(&snap->data()); }
    void run(int frames, int block = 256) {
        std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        int pos = 0;
        while (pos < frames) {
            const int n = std::min(block, frames - pos);
            std::fill(l.begin(), l.end(), 0.f);
            std::fill(r.begin(), r.end(), 0.f);
            seq.process(frame, n, l.data(), r.data());
            audio.insert(audio.end(), l.begin(), l.begin() + n);
            while (auto e = seq.out.pop()) outs.push_back(*e);
            frame += n;
            pos += n;
        }
    }
    std::vector<int64_t> trigs(int track = 0) const {
        std::vector<int64_t> v;
        for (const auto& e : outs)
            if (e.kind == SeqOut::Kind::trig && e.track == track) v.push_back(e.frame);
        return v;
    }
};

void sampleAccurate() {
    Project p;
    for (int s = 0; s < 16; s += 4) p.tracks[0].pagesMut()[0][size_t(s)].on = true;
    Rig r(p);
    r.seq.setPlaying(true, 0);
    r.run(48000 * 2 + 100, 128);
    check(r.trigs() == std::vector<int64_t>{0, 24000, 48000, 72000, 96000}, "seq: шаги с точностью до отсчёта");
}

void clockPhase() {
    Project p;
    for (int s = 0; s < 16; ++s) p.tracks[0].pagesMut()[0][size_t(s)].on = true;
    Rig r(p);
    r.seq.start(1000, 0);
    int sent = -1;
    while (r.frame < 6000 * 40) {
        const int k = int(r.seq.clock.read().nextTick);
        if (k != sent) {
            r.seq.clockPhase(k, double(1300 + 6000 * k));
            sent = k;
        }
        r.run(256);
    }
    const auto t = r.trigs();
    check(!t.empty() && t[0] == 1075, "clock: шаг 0 — на четверть ближе к клоку");
    check(t.size() > 39 && std::fabs(double(t[30]) - (1300 + 6000 * 30)) <= 2 &&
              std::fabs(double(t[39]) - (1300 + 6000 * 39)) <= 2,
          "clock: фаза подтянулась");
    Project q;
    q.tracks[0].pagesMut()[0][0].on = true;
    Rig late(q);
    late.run(48000);
    late.seq.start(0, late.frame);
    late.run(512);
    check(!late.trigs().empty() && late.trigs()[0] == 48000 - 5400, "clock: старт в прошлом — не раньше 0,9 шага");
}

void metronome() {
    Rig r{Project()};
    r.seq.metronome = true;
    r.seq.setPlaying(true, 0);
    r.run(30000);
    check(r.audio[1] != 0 && r.audio[23999] == 0 && r.audio[24001] != 0, "metronome: доли с точностью до отсчёта");
    float a = 0, b = 0;
    for (int i = 0; i < 200; ++i) {
        a = std::max(a, std::fabs(r.audio[size_t(i)]));
        b = std::max(b, std::fabs(r.audio[size_t(24000 + i)]));
    }
    check(a > b, "metronome: акцент громче");
}

void ratchetSwing() {
    Project p;
    auto& pg = p.tracks[0].pagesMut()[0];
    pg[0].on = true;
    pg[0].locks["ratchet"] = 3;
    pg[1].on = true;
    p.tracks[0].params["swing"] = 75;
    Rig r(p);
    r.seq.setPlaying(true, 0);
    r.run(12000);
    check(r.trigs() == std::vector<int64_t>{0, 1500, 3000, 4500, 9000}, "seq: рэтчет и свинг");
    Project q;
    q.tracks[0].pagesMut()[0][0].on = true;
    q.tracks[0].pagesMut()[0][1].on = true;
    q.tracks[0].pagesMut()[0][1].locks["swing"] = 75;
    Rig s(q);
    s.seq.setPlaying(true, 0);
    s.run(12000);
    check(s.trigs() == std::vector<int64_t>{0, 6000}, "seq: p-lock swing не действует");
}

/// Запуск клипа во время игры — когда текущий доиграет (16 шагов = 96000 кадров).
void launch() {
    Project p;
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].putEmptyClip(1, 0, 0);
    p.tracks[0].clips[1]->pages[0][2].on = true;
    Rig r(p);
    r.seq.setPlaying(true, 0);
    r.run(6000 * 3);
    r.seq.queue(0, 1);
    r.run(6000 * 20);
    check(r.trigs() == std::vector<int64_t>{0, 96000 + 12000}, "launch: клип — в конце прохода");
    bool clipMsg = false;
    for (const auto& e : r.outs)
        if (e.kind == SeqOut::Kind::clip && e.track == 0 && e.a == 1 && e.b == 16) clipMsg = true;
    check(clipMsg, "launch: сообщение clip с тика 16");
}
}  // namespace

int runSeq() {
    fails = 0;
    sampleAccurate();
    clockPhase();
    metronome();
    ratchetSwing();
    launch();
    std::printf("seq: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
