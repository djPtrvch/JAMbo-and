// Секвенсор через ядро: порт SequencerTests (ветка swift JAMbo) — AudioCore.render, события CoreEvent, снимки.
// testPatternLengthAndPages, testSwingMigration и testMixLocksFollowTracks — модельные, тоже здесь (как в Swift).
#include "engine/AudioCore.h"
#include "model/Params.h"
#include "model/Project.h"
#include "sequencer/SeqSnapshot.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

struct Run {
    std::vector<float> audio;
    std::vector<SeqOut> out;
};
/// Прогнать ядро на frames кадров блоками по block: левый канал и сообщения секвенсора.
Run run(AudioCore& core, int frames, int block = 256) {
    Run r;
    r.audio.assign(size_t(frames), 0.f);
    std::vector<float> right(static_cast<size_t>(block));
    for (int pos = 0; pos < frames;) {
        const int n = std::min(block, frames - pos);
        core.render(n, r.audio.data() + pos, right.data());
        while (auto e = core.sequencer.out.pop()) r.out.push_back(*e);
        pos += n;
    }
    return r;
}
std::vector<int64_t> trigs(const std::vector<SeqOut>& out, int track = 0) {
    std::vector<int64_t> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::trig && e.track == track) v.push_back(e.frame);
    return v;
}

void sampleAccurate() {
    Project p;
    for (int s = 0; s < 16; s += 4) p.tracks[0].pagesMut()[0][size_t(s)].on = true;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    const auto r = run(core, 48000 * 2 + 100, 128);
    check(trigs(r.out) == std::vector<int64_t>{0, 24000, 48000, 72000, 96000}, "core seq: шаги с точностью до отсчёта");
}

/// MIDI-клок: старт в заданном кадре; фаза шагов подтягивается к тикам клока на 25 % за шаг.
void clockPhase() {
    Project p;
    for (int s = 0; s < 16; ++s) p.tracks[0].pagesMut()[0][size_t(s)].on = true;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::playAt(1000));
    std::vector<int64_t> list;
    std::vector<float> l(256), r(256);
    int sent = -1;
    for (int pos = 0; pos < 6000 * 40; pos += 256) {
        core.render(256, l.data(), r.data());
        while (auto e = core.sequencer.out.pop())
            if (e->kind == SeqOut::Kind::trig) list.push_back(e->frame);
        const int k = int(core.sequencer.clock.read().nextTick);
        if (k != sent) {
            core.events.push(CoreEvent::clockPhase(k, double(1300 + 6000 * k)));
            sent = k;
        }
    }
    check(!list.empty() && list[0] == 1075, "core clock: шаг 0 — на четверть ближе к клоку");
    check(list.size() > 39 && std::fabs(double(list[30]) - (1300 + 6000 * 30)) <= 2 &&
              std::fabs(double(list[39]) - (1300 + 6000 * 39)) <= 2,
          "core clock: фаза подтянулась");
    Project q;
    q.tracks[0].pagesMut()[0][0].on = true;
    AudioCore late;
    late.publish(q);
    run(late, 48000);
    late.events.push(CoreEvent::playAt(0));
    const auto t = trigs(run(late, 512).out);
    check(!t.empty() && t[0] == 48000 - 5400, "core clock: старт в прошлом — не раньше 0,9 шага");
}

void metronome() {
    AudioCore core;
    core.publish(Project());
    core.events.push(CoreEvent::metronome(true));
    core.events.push(CoreEvent::play(true));
    const auto r = run(core, 30000);
    // Тишина: Delay добавляет 1e-18 против денормалов (ниже −240 dB).
    check(r.audio[1] != 0 && std::fabs(r.audio[23999]) <= 1e-12f && r.audio[24001] != 0,
          "core metronome: доли с точностью до отсчёта");
    float a = 0, b = 0;
    for (int i = 0; i < 200; ++i) {
        a = std::max(a, std::fabs(r.audio[size_t(i)]));
        b = std::max(b, std::fabs(r.audio[size_t(24000 + i)]));
    }
    check(a > b, "core metronome: акцент громче");
}

void ratchetSwing() {
    Project p;
    auto& pg = p.tracks[0].pagesMut()[0];
    pg[0].on = true;
    pg[0].locks["ratchet"] = 3;  // 4 удара
    pg[1].on = true;
    p.tracks[0].params["swing"] = 75;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    check(trigs(run(core, 12000).out) == std::vector<int64_t>{0, 1500, 3000, 4500, 9000}, "core seq: рэтчет и свинг");
    Project q;
    q.tracks[0].pagesMut()[0][0].on = true;
    q.tracks[0].pagesMut()[0][1].on = true;
    q.tracks[0].pagesMut()[0][1].locks["swing"] = 75;
    AudioCore s;
    s.publish(q);
    s.events.push(CoreEvent::play(true));
    check(trigs(run(s, 12000).out) == std::vector<int64_t>{0, 6000}, "core seq: p-lock swing не действует");
}

/// Версия 3: p-lock swing и назначения LFO/ENV на swing снимаются, значение дорожки остаётся.
void swingMigration() {
    Project p;
    p.version = 2;
    p.tracks[0].params["swing"] = 66;
    p.tracks[0].assigns["swing"] = ModAssign(1, 0.5);
    p.tracks[0].assigns["pan"] = ModAssign(2, 0.3);
    auto& st = p.tracks[0].pagesMut()[0][3];
    st.locks = {{"swing", 70}, {"level", 90}};
    st.soundLocks = std::vector<std::string>{"swing", "level"};
    p.repair();
    const auto& s3 = p.tracks[0].pages()[0][3];
    check(p.version == 10 && p.tracks[0].params["swing"] == 66 && !p.tracks[0].assigns.count("swing") &&
              p.tracks[0].assigns.count("pan"),
          "migration: swing дорожки остаётся, назначение снято");
    check(s3.locks == ParamMap{{"level", 90}} && s3.soundLocks == std::vector<std::string>{"level"},
          "migration: p-lock swing снят, level остаётся");
    check(Project().version == 10, "migration: версия нового проекта");
}

void chanceZero() {
    Project p;
    p.tracks[0].pagesMut()[0][0].on = true;
    p.tracks[0].pagesMut()[0][0].locks["prob"] = 0;
    p.tracks[1].pagesMut()[0][0].on = true;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    const auto r = run(core, 48000 * 4);
    check(trigs(r.out, 0).empty() && trigs(r.out, 1).size() == 2, "core seq: Chance 0 не играет");
}

/// Смена темпа на ходу: новый темп — со следующего шага, доля не прыгает.
void tempoChange() {
    Project p;
    for (int s = 0; s < 16; ++s) p.tracks[0].pagesMut()[0][size_t(s)].on = true;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    const bool first = trigs(run(core, 13000).out) == std::vector<int64_t>{0, 6000, 12000};
    core.events.push(CoreEvent::tempo(60));
    check(first && trigs(run(core, 24000).out) == std::vector<int64_t>{18000, 30000}, "core seq: смена темпа");
}

void patternLength() {
    Track t(0);
    t.pagesMut() = {emptyPage(), emptyPage(), emptyPage()};
    t.setPlayTo(2);
    t.setLength(40);
    bool ok = t.playLen() == 40 && t.playIndex(39) == 39 && t.playIndex(40) == 0;
    t.setPlayTo(1);
    ok = ok && t.playLen() == 32;
    t.setPlayFrom(2);
    t.setPlayTo(2);
    ok = ok && t.playLen() == 8 && t.playIndex(9) == 33 && t.outside(2 * 16 + 8);
    check(ok, "pattern: длина и страницы");
    Project p;
    p.tracks[0] = t;
    const SeqSnapshot snap(p);
    check(snap.data().playLen(0) == 8 && snap.data().playIndexActive(0, 9) == 33, "pattern: снимок считает так же");
}

void stopRestart() {
    Project p;
    p.tracks[0].pagesMut()[0][0].on = true;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    run(core, 10000);
    core.events.push(CoreEvent::play(false));
    const bool quiet = trigs(run(core, 100000).out).empty();
    core.events.push(CoreEvent::play(true));
    check(quiet && trigs(run(core, 1000).out) == std::vector<int64_t>{110000},
          "core seq: стоп и снова с шага 0 в первом кадре блока");
}

void snapshotsReleased() {
    const int before = SeqSnapshot::liveCount;
    {
        AudioCore core;
        for (int k = 0; k < 10; ++k) {
            Project p;
            p.tracks[0].pagesMut()[0][size_t(k)].on = true;
            core.publish(p, {}, {}, 0);
            run(core, 256);
        }
        // Прежние снимки держатся keepAlive секунд.
        core.releaseRetired(0);
        const bool kept = SeqSnapshot::liveCount - before > 1;
        core.releaseRetired(10);
        check(kept && SeqSnapshot::liveCount - before == 1, "core: в памяти только текущий снимок");
    }
    check(Params::noteName(48) == "C3" && Params::noteName(37) == "C#2", "noteName");
}

/// MIX и MST: идут с PLAY с первого шага по своей длине; p-lock громкости на шаге MIX — пока шаг играет.
void channelSequences() {
    Project p;
    p.chanSeqs[Project::mixSeq].setLength(4);
    p.chanSeqs[Project::mixSeq].setLock(1, "mix.vol2", -6);
    p.chanSeqs[Project::mstSeq].setLength(3);
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    std::vector<int> mixPos, mstPos;
    std::vector<uint16_t> masks;
    std::vector<float> gains;
    for (int i = 0; i < 6; ++i) {
        const auto r = run(core, 6000, 250);
        for (const auto& e : r.out) {
            if (e.kind != SeqOut::Kind::step) continue;
            if (e.track == SeqData::maxTracks + Project::mixSeq) mixPos.push_back(e.a);
            if (e.track == SeqData::maxTracks + Project::mstSeq) mstPos.push_back(e.a);
        }
        masks.push_back(core.sequencer.mixLockMask);
        gains.push_back(core.sequencer.mixLockGain[1]);
    }
    check(mixPos == std::vector<int>{0, 1, 2, 3, 0, 1} && mstPos == std::vector<int>{0, 1, 2, 0, 1, 2},
          "core chan: шаги MIX и MST");
    check(masks == std::vector<uint16_t>{0, 2, 0, 0, 0, 2} && std::fabs(gains[1] - float(Params::volGain(-6))) <= 1e-6f,
          "core chan: p-lock громкости на шаге MIX");
    core.events.push(CoreEvent::play(false));
    run(core, 256);
    check(core.sequencer.mixLockMask == 0, "core chan: стоп — p-lock снят");
}

/// P-lock громкостей на шагах MIX переезжают с дорожками (удаление, перестановка).
void mixLocksFollow() {
    Project p;
    for (int i = 0; i < 2; ++i) p.addTrack();
    auto& mix = p.chanSeqs[Project::mixSeq];
    mix.setLock(0, "mix.vol3", -1);
    mix.setLock(0, "mix.vol5", -2);
    mix.setLock(0, "mix.vol9", -3);
    p.deleteTrack(2);
    const auto locks = [&] { return p.chanSeqs[Project::mixSeq].step(0)->locks; };
    const bool del = locks() == ParamMap{{"mix.vol4", -2}, {"mix.vol8", -3}};
    p.moveTrack(3, 0);
    const bool mv = locks() == ParamMap{{"mix.vol1", -2}, {"mix.vol8", -3}};
    p.moveTrack(0, 7);
    check(del && mv && locks() == ParamMap{{"mix.vol8", -2}, {"mix.vol7", -3}}, "mix locks: за дорожками");
}
}  // namespace

int runSeqCoreTests() {
    sampleAccurate();
    clockPhase();
    metronome();
    ratchetSwing();
    swingMigration();
    chanceZero();
    tempoChange();
    patternLength();
    stopRestart();
    snapshotsReleased();
    channelSequences();
    mixLocksFollow();
    std::printf("seq core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
