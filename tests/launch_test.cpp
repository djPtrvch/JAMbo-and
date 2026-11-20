// Запуск клипов и строк через ядро: порт LaunchTests (ветка swift JAMbo) — без testLaunchesDuringMix (переход MIX —
// Project::mixed ещё не перенесён); правки строк и клипов — testSceneTapStartsAtBarAndSwitchAtPass (модельная часть),
// testMoveRow, testClipCopyAcrossTracksCarriesSteps, testRowEditsFollowBlocks (ArrangeTests).
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

Page& pg(Track& t, int k, int page = 0) { return t.clips[size_t(k)]->pages[size_t(page)]; }

void rowEdits() {
    Project p = project();
    p.rows = 3;
    p.fillRows();
    bool full = true;
    for (int k = 0; k < 3; ++k)
        for (const auto& t : p.tracks) full = full && t.clipFilled(k);
    check(full, "rows: пустых ячеек нет");
    check(p.tracks[0].clips[2]->pages.size() == 1, "rows: новый клип — свой пустой паттерн");
    pg(p.tracks[0], 0)[0].on = true;
    p.tracks[1].stopped = true;
    check(!p.plusCopy, "rows: по умолчанию NEW PATTERNS");
    p.plusCopy = true;
    p.addRow();
    check(p.tracks[0].clips[3] == p.tracks[0].clips[0], "rows: COPY PLAYING — копия играющего клипа");
    check(!pg(p.tracks[1], 3)[0].on, "rows: остановленная — пустой паттерн");
    pg(p.tracks[0], 3)[1].locks["pan"] = 10;
    check(!pg(p.tracks[0], 0)[1].locks.count("pan"), "rows: копия независима");
    p.setScene(1, "  Chorus  ", 99);
    check(p.scenes[1].name == "Chorus" && p.scenes[1].repeats == 64, "rows: setScene");
    p.duplicateRow(1);
    check(p.scenes[2].name == "Chorus" && p.visibleRows() == 5, "rows: DUPLICATE");
    p.tracks[0].activate(2);
    check(p.tracks[0].open == 2, "rows: открытый идёт за активным");
    p.deleteRow(2);
    check(p.tracks[0].stopped && p.visibleRows() == 4 && p.sceneName(0) == "1", "rows: DELETE строки");
    Track t = p.tracks[2];
    pg(t, 1)[0].on = true;
    for (int i = 0; i < 3; ++i) t.clips[1]->pages.push_back(emptyPage());
    t.clips[1]->length = 64;
    t.clips[1]->playTo = 3;
    pg(t, 0)[0].on = true;
    t.clearClip(1);
    const auto r = t.clips[1]->range();
    check(!pg(t, 1)[0].on && t.clips[1]->pages.size() == 1 && t.clips[1]->length == 16 && r.lower == 0 && r.upper == 0,
          "rows: DELETE клипа — одна пустая страница");
    check(pg(t, 0)[0].on, "rows: другие клипы не тронуты");
}

void moveRows() {
    Project p;
    p.rows = 4;
    p.fillRows();
    for (int k = 0; k < 4; ++k) pg(p.tracks[0], k)[size_t(k)].on = true;
    p.setScene(0, "A", 2);
    p.setScene(3, "D", 1);
    p.tracks[0].activate(0);
    p.tracks[1].activate(2);
    p.arrStart = 3;
    p.moveRow(0, 2);
    check(p.scenes[2].name == "A" && p.scenes[2].repeats == 2 && pg(p.tracks[0], 2)[0].on && pg(p.tracks[0], 0)[1].on,
          "moveRow: строка и клипы на новом месте");
    check(p.tracks[0].slot == 2 && p.tracks[0].open == 2 && p.tracks[1].slot == 1 && p.arrStart == 3,
          "moveRow: активные ячейки за строками");
    p.moveRow(3, 0);
    check(p.scenes[0].name == "D" && p.arrStart == 0 && p.tracks[0].slot == 3, "moveRow: строка старта за строкой");
    p.moveRow(1, 9);
    check(p.scenes[0].name == "D", "moveRow: за пределы — без изменений");
}

void clipCopyAcross() {
    Project p;
    pg(p.tracks[3], 0)[4].on = true;
    pg(p.tracks[3], 0)[4].locks["filter"] = 20;
    p.rows = 4;
    p.fillRows();
    const auto c = p.tracks[3].clips[0];
    p.tracks[5].setClip(2, c);
    p.tracks[3].setClip(3, c);
    check(pg(p.tracks[5], 2)[4].on && pg(p.tracks[5], 2)[4].locks["filter"] == 20 && pg(p.tracks[3], 3)[4].on,
          "clips: копия в другой столбец — с шагами");
    p.tracks[3].clearClip(0);
    check(pg(p.tracks[3], 3)[4].on, "clips: копия не очищается с исходным");
}

void rowBlocks() {
    Project p;
    p.addRow();
    p.addRow();
    p.addRow();
    for (int k = 0; k < 3; ++k) {
        ArrBlock b;
        b.clip = k;
        b.start = 16 * k;
        b.length = 16;
        p.tracks[0].placeBlock(b);
    }
    auto ids = [&] {
        std::vector<int> v;
        for (const auto& b : p.tracks[0].blocks) v.push_back(b.clip);
        return v;
    };
    p.deleteRow(1);
    check(ids() == std::vector<int>{0, 1}, "rows: блок удалённого клипа уходит");
    p.moveRow(0, 1);
    check(ids() == std::vector<int>{1, 0}, "rows: moveRow — блоки за строками");
    p.duplicateRow(0);
    check(ids() == std::vector<int>{2, 0}, "rows: duplicateRow — блоки");
    p.pasteRow(0, p.copyRow(1));
    check(ids() == std::vector<int>{2}, "rows: PASTE — блоки заменённого клипа уходят");
}

int runLaunchTests() {
    rowEdits();
    moveRows();
    clipCopyAcross();
    rowBlocks();
    moveTrack();
    queued();
    speed();
    stoppedAndToggle();
    scenesRun();
    std::printf("launch: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
