// Запуск клипов и строк через ядро: порт LaunchTests (ветка swift JAMbo) — без testLaunchesDuringMix (переход MIX —
// Project::mixed ещё не перенесён) и без чисто модельных (правки строк и клипов).
#include "engine/AudioCore.h"
#include "model/Project.h"
#include "sequencer/Launcher.h"
#include <algorithm>
#include <cstdio>
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
std::vector<SeqOut> run(AudioCore& core, int frames) {
    std::vector<float> l(256), r(256);
    std::vector<SeqOut> out;
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, l.data(), r.data());
        while (auto e = core.sequencer.out.pop()) out.push_back(*e);
        pos += n;
    }
    return out;
}
struct ClipEv {
    int slot;
    bool stopped;
    int start;
};
std::vector<ClipEv> clips(const std::vector<SeqOut>& out, int t) {
    std::vector<ClipEv> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::clip && e.track == t) v.push_back({e.a, e.on, e.b});
    return v;
}
std::vector<std::pair<int, int>> scenes(const std::vector<SeqOut>& out) {
    std::vector<std::pair<int, int>> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::scene && e.a >= 0) v.push_back({e.a, e.b});
    return v;
}
std::vector<int64_t> trigs(const std::vector<SeqOut>& out, int t) {
    std::vector<int64_t> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::trig && e.track == t) v.push_back(e.frame);
    return v;
}
Project project() {
    Project p;
    Clip c;
    c.pages = {emptyPage(), emptyPage()};
    c.length = 28;
    c.playFrom = 1;
    c.playTo = 1;
    p.tracks[0].clips[1] = c;
    return p;
}

void moveTrack() {
    Project p;
    for (int i = 0; i < 16; ++i) p.tracks[0].clips[0]->pages[0][size_t(i)].on = true;
    for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0] = std::nullopt;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    run(core, 6000 * 2);
    p.moveTrack(0, 3);
    core.publish(p);
    core.events.push(CoreEvent::moveTrack(0, 3));
    const auto out = run(core, 6000 * 2);
    bool any = false, all3 = true;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::trig) {
            any = true;
            all3 = all3 && e.track == 3;
        }
    check(any && all3, "launch: перестановка во время игры — без паузы");
}

void queued() {
    AudioCore core;
    core.publish(project());
    core.events.push(CoreEvent::play(true));
    run(core, 6000 * 3);
    core.events.push(CoreEvent::launch(0, 1));
    const auto out = run(core, 6000 * 20);
    const auto c = clips(out, 0);
    bool step16 = false;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::step && e.track == 0 && e.a == 16) step16 = true;
    check(!c.empty() && c[0].slot == 1 && c[0].start == 16 && step16, "launch: клип — в конце прохода");
}

void speed() {
    auto tr = [](std::optional<int> rate) {
        Project p = project();
        for (int i = 0; i < 16; ++i) p.tracks[0].pagesMut()[0][size_t(i)].on = true;
        p.tracks[0].clips[0]->rate = rate;
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        return trigs(run(core, 6000 * 12), 0);
    };
    const auto n = tr(std::nullopt), h = tr(-1), d = tr(1), s = tr(-4);
    check(n.size() > 1 && n[1] - n[0] == 6000 && h[1] - h[0] == 12000 && d[1] - d[0] == 3000 &&
              std::abs(int(d.size()) - (2 * int(n.size()) - 1)) <= 1 && s.size() == 1,
          "launch: скорость клипа");
    Project p = project();
    p.tracks[0].clips[0]->rate = -1;
    AudioCore core;
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    run(core, 6000 * 3);
    core.events.push(CoreEvent::launch(0, 1));
    const auto c = clips(run(core, 6000 * 40), 0);
    check(!c.empty() && c[0].start == 32, "launch: после медленного клипа — проход 32 тика");
}

void stoppedAndToggle() {
    {
        Project p = project();
        p.tracks[0].stopped = true;
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        run(core, 6000 * 5);
        core.events.push(CoreEvent::launch(0, 1));
        const auto c = clips(run(core, 6000 * 20), 0);
        check(!c.empty() && c[0].start == 16, "launch: остановленная — с начала такта");
    }
    AudioCore core;
    core.publish(project());
    core.events.push(CoreEvent::play(true));
    run(core, 6000);
    core.events.push(CoreEvent::launch(0, 1));
    core.events.push(CoreEvent::launch(0, 1));
    const bool cancelled = clips(run(core, 6000 * 20), 0).empty();
    core.events.push(CoreEvent::launch(0, Launcher::stop));
    const auto c = clips(run(core, 6000 * 20), 0);
    check(cancelled && !c.empty() && c[0].stopped, "launch: повторный тап — отмена, STOP");
}

void scenesRun() {
    {
        Project p = project();
        p.rows = 2;
        p.rowLoop = false;
        p.scenes[0].repeats = 2;
        p.fillRows();
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        const auto s = scenes(run(core, 6000 * 70));
        check(s == std::vector<std::pair<int, int>>{{0, 0}, {1, 32}, {0, 48}}, "launch: строки с повторами и по кругу");
    }
    {
        Project p = project();
        p.rows = 2;
        p.rowLoop = false;
        p.arrEndStop = true;
        p.fillRows();
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        const auto out = run(core, 6000 * 40);
        const bool ended =
            std::any_of(out.begin(), out.end(), [](const SeqOut& e) { return e.kind == SeqOut::Kind::ended; });
        check(ended && !core.sequencer.playing(), "launch: AT THE END STOP");
    }
    {
        Project p = project();
        p.rows = 2;
        p.fillRows();
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        const auto s = scenes(run(core, 6000 * 80));
        check(s.size() == 1 && s[0].first == 0, "launch: LOOP ROW");
    }
    {
        Project p;
        p.tracks[0].pagesMut()[0][0].on = true;
        p.tracks[0].clips[1] = Clip();
        p.tracks[0].clips[1]->pages[0][2].on = true;
        p.arrStart = 1;
        p.rows = 2;
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        const auto t = trigs(run(core, 6000 * 8), 0);
        check(!t.empty() && t[0] == 12000, "launch: клип играет свои шаги");
    }
}
}  // namespace

int runLaunchTests() {
    moveTrack();
    queued();
    speed();
    stoppedAndToggle();
    scenesRun();
    std::printf("launch: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
