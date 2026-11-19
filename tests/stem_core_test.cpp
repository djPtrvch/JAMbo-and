// Запись стемов через ядро: порт StemTests (ветка swift JAMbo) — начало с PLAY, конец на границах проходов,
// с точностью до отсчёта; звук — настоящий сэмплер и микшер ядра.
#include "engine/AudioCore.h"
#include "engine/StemRing.h"
#include "model/Project.h"
#include "sequencer/Launcher.h"
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

/// Шаг при 120 BPM — 6000 кадров, клип в одну страницу — 16 шагов.
constexpr int step = 6000;
constexpr int pass = 16 * 6000;

struct Run {
    std::vector<float> l, r;
    std::map<int, SampleRef> stems;
    bool ended = false;
    bool once = true;  // стем канала — один, без пропусков
};

void run(AudioCore& core, StemCollector& col, int frames, Run& o) {
    std::vector<float> bl(256), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, bl.data(), br.data());
        o.l.insert(o.l.end(), bl.begin(), bl.begin() + n);
        o.r.insert(o.r.end(), br.begin(), br.begin() + n);
        while (auto e = core.sequencer.out.pop())
            if (e->kind == SeqOut::Kind::ended) o.ended = true;
        for (auto& s : col.poll(core.sequencer.stemRing)) {
            if (s.dropped != 0 || o.stems.count(s.channel)) o.once = false;
            o.stems[s.channel] = s.data;
        }
        pos += n;
    }
}

int framesOf(const Run& o, int ch) { return o.stems.count(ch) ? o.stems.at(ch)->frames() : -1; }

/// Дорожка 0 — постоянный сигнал на шаге 0 (держится весь проход), громкость 0 dB.
Project project() {
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].vol = 0;
    auto& s = p.tracks[0].pagesMut()[0][0];
    s.on = true;
    s.notes = {48};
    s.len = 16;
    return p;
}

/// Строки по сетке: строка 0 — 3 повтора, строка 1 — 1; стем A, A, A, B.
Project arranged() {
    Project p = project();
    p.rows = 2;
    p.rowLoop = false;
    p.fillRows();
    p.scenes[0].repeats = 3;
    p.scenes[1].repeats = 1;
    return p;
}

std::unique_ptr<AudioCore> start(const Project& p, std::vector<int> arm, bool master = false) {
    auto core = std::make_unique<AudioCore>(master);
    std::map<std::string, SampleRef> samples;
    samples["k"] = std::make_shared<SampleData>(std::vector<float>(size_t(48000 * 20), 1.f), nullptr);
    core->publish(p, samples);
    core->sequencer.stemRing.prepare();
    for (int c : arm) core->events.push(CoreEvent::stemArm(c, true));
    core->events.push(CoreEvent::play(true));
    return core;
}

void onePass() {
    auto core = start(project(), {0});
    Run o;
    StemCollector col;
    run(*core, col, pass * 3, o);
    check(framesOf(o, 0) == pass && o.once, "core stems: клип по кругу — один проход");
    if (framesOf(o, 0) != pass) return;
    const auto& s = *o.stems[0];
    float diff = 0;
    for (int i = 0; i < pass; ++i)
        diff = std::max({diff, std::fabs(s.left()[i] - o.l[size_t(i)]), std::fabs(s.right()[i] - o.r[size_t(i)])});
    check(diff < 1e-6f && s.left()[1000] > 0.5f, "core stems: стем = выход по отсчётам");
    check(core->sequencer.playing() && !o.stems.count(1) && !o.stems.count(StemRing::master),
          "core stems: игра продолжается, неготовые не пишутся");
}

void muted() {
    Project p = project();
    p.tracks[0].mute = true;
    auto core = start(p, {0});
    Run o;
    StemCollector col;
    run(*core, col, pass * 2, o);
    float m = 1;
    if (framesOf(o, 0) == pass) {
        m = 0;
        for (int i = 0; i < pass; ++i) m = std::max(m, std::fabs(o.stems[0]->left()[i]));
    }
    check(m <= 1e-6f, "core stems: стем — после mute/solo");
}

void rows() {
    {
        auto core = start(arranged(), {0});
        Run o;
        StemCollector col;
        run(*core, col, pass * 6, o);
        check(framesOf(o, 0) == pass * 4 && core->sequencer.playing(), "core stems: строки по сетке — A, A, A, B");
    }
    {
        Project p = project();
        p.rows = 2;
        p.fillRows();
        p.scenes[0].repeats = 3;
        auto core = start(p, {0});
        Run o;
        StemCollector col;
        run(*core, col, pass * 3, o);
        check(framesOf(o, 0) == pass, "core stems: LOOP ROW — один раз");
    }
}

void finishAndStop() {
    {
        auto core = start(arranged(), {0});
        Run o;
        StemCollector col;
        run(*core, col, step * 3, o);
        const bool rec = col.recording() == std::set<int>{0};
        core->events.push(CoreEvent::stemFinish(0));
        run(*core, col, pass * 2, o);
        check(rec && framesOf(o, 0) == pass && core->sequencer.playing() && !core->sequencer.stemArmed(0),
              "core stems: тап по ● — до конца прохода, готовность снята");
    }
    {
        auto core = start(arranged(), {0});
        Run o;
        StemCollector col;
        run(*core, col, step * 3, o);
        core->events.push(CoreEvent::make(CoreEvent::Kind::stopAfterStems));
        run(*core, col, pass * 2, o);
        check(framesOf(o, 0) == pass && !core->sequencer.playing() && o.ended, "core stems: PLAY — дописать и стоп");
    }
    {
        auto core = start(project(), {});
        Run o;
        StemCollector col;
        run(*core, col, step, o);
        core->events.push(CoreEvent::make(CoreEvent::Kind::stopAfterStems));
        run(*core, col, 256, o);
        check(!core->sequencer.playing() && o.ended, "core stems: без записи — стоп сразу");
    }
    {
        auto core = start(arranged(), {0});
        Run o;
        StemCollector col;
        run(*core, col, 256 * 100, o);
        core->events.push(CoreEvent::play(false));
        run(*core, col, 2560, o);
        check(framesOf(o, 0) == 256 * 100, "core stems: стоп — конец в кадре остановки");
    }
    {
        auto core = start(project(), {0});
        Run o;
        StemCollector col;
        run(*core, col, step * 2, o);
        check(col.endAt().count(0) && col.endAt().at(0) == pass, "core stems: конец виден — граница прохода");
    }
}

void master() {
    {
        auto core = start(project(), {StemRing::master}, true);
        Run o;
        StemCollector col;
        run(*core, col, pass * 3, o);
        float diff = 1;
        if (framesOf(o, StemRing::master) == pass) {
            diff = 0;
            for (int i = 0; i < pass; ++i)
                diff = std::max(diff, std::fabs(o.stems[StemRing::master]->left()[i] - o.l[size_t(i)]));
        }
        check(diff < 1e-6f, "core stems: мастер — проход, конечный выход");
    }
    {
        auto core = start(arranged(), {StemRing::master, 0}, true);
        Run o;
        StemCollector col;
        run(*core, col, pass * 6, o);
        check(framesOf(o, StemRing::master) == pass * 4 && framesOf(o, 0) == pass * 4,
              "core stems: мастер по строкам — до круга");
    }
}

void launches() {
    {
        Project p = project();
        Clip c;
        c.pages = {emptyPage(), emptyPage()};
        c.length = 28;
        c.playFrom = 1;
        c.playTo = 1;
        p.tracks[0].clips[1] = c;
        auto core = start(p, {0});
        Run o;
        StemCollector col;
        run(*core, col, step * 3, o);
        core->events.push(CoreEvent::launch(0, 1));
        run(*core, col, pass * 3, o);
        check(framesOf(o, 0) == pass + 12 * step, "core stems: другой клип — A, B, конец на повторе");
    }
    {
        auto core = start(project(), {0});
        Run o;
        StemCollector col;
        run(*core, col, step * 3, o);
        core->events.push(CoreEvent::launch(0, Launcher::stop));
        run(*core, col, pass * 2, o);
        check(framesOf(o, 0) == pass, "core stems: дорожка остановилась — там же конец");
    }
}
}  // namespace

int runStemCoreTests() {
    onePass();
    muted();
    rows();
    finishAndStop();
    master();
    launches();
    std::printf("stem core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
