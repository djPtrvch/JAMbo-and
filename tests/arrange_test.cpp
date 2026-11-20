// Аранжировщик через ядро и его модель: порт ArrangePlayTests, ArrangeOwnTests (+ ArrangeFiredTests без
// testRecordSettingsArrStart — RecordSettings не перенесены, FiredOutTests, ArrangeEditBlockTests) и
// ArrangeAutoReturnTests ветки swift JAMbo.
#include "engine/AudioCore.h"
#include "model/FilterType.h"
#include "model/Json.h"
#include "model/Project.h"
#include "sampler/Biquad.h"
#include "sampler/VoiceParam.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
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
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

constexpr int stepF = 6000;
using Out = std::vector<SeqOut>;

Out run(AudioCore& core, int steps, const std::function<void(int)>& each = nullptr) {
    std::vector<float> l(256), r(256);
    Out out;
    const int frames = steps * stepF;
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, l.data(), r.data());
        while (auto e = core.sequencer.out.pop()) out.push_back(*e);
        if (each) each(pos);
        pos += n;
    }
    return out;
}
std::vector<std::vector<int>> clipsOf(const Out& out, int t) {
    std::vector<std::vector<int>> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::clip && e.track == t) v.push_back({e.a, e.on ? 1 : 0, e.b});
    return v;
}
std::vector<int> stepsOf(const Out& out, int t) {
    std::vector<int> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::step && e.track == t) v.push_back(e.a);
    return v;
}
std::vector<int> cursor(const Out& out) {
    std::vector<int> v;
    for (const auto& e : out)
        if (e.kind == SeqOut::Kind::arrange) v.push_back(e.a);
    return v;
}
bool has(const Out& out, SeqOut::Kind k, int track = -1, int on = -1) {
    for (const auto& e : out)
        if (e.kind == k && (track < 0 || e.track == track) && (on < 0 || e.on == (on != 0))) return true;
    return false;
}
std::vector<int> slice(const std::vector<int>& v, size_t a, size_t b) {
    if (v.size() < b) return {};
    return std::vector<int>(v.begin() + long(a), v.begin() + long(b));
}
std::vector<int> range(int a, int b) {
    std::vector<int> v;
    for (int i = a; i < b; ++i) v.push_back(i);
    return v;
}

/// Дорожка 0: клип 0 — 16 шагов, клип 1 — 8 шагов; остальные дорожки пустые.
Project project() {
    Project p;
    Clip c;
    c.length = 8;
    p.tracks[0].clips[1] = c;
    for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0].reset();
    return p;
}
std::unique_ptr<AudioCore> start(const Project& p, int from = 0, bool master = true, bool synth = false) {
    auto core = std::make_unique<AudioCore>(master);
    if (synth) core->installSynth();
    core->publish(p);
    core->events.push(CoreEvent::arrange(true, from));
    core->events.push(CoreEvent::play(true));
    return core;
}
float param(AudioCore& c, int k) { return c.sampler.sound()->params[size_t(k)]; }

// MARK: — ArrangePlayTests

void play() {
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(1, 4, 12));
        p.tracks[0].placeBlock(ArrBlock(0, 20, 6));
        const auto out = run(*start(p), 30);
        const auto s = stepsOf(out, 0);
        check(clipsOf(out, 0) == std::vector<std::vector<int>>{{0, 1, 0}, {1, 0, 4}, {1, 1, 16}, {0, 0, 20}} &&
                  slice(s, 4, 16) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3} &&
                  slice(s, 16, 20) == std::vector<int>{-1, -1, -1, -1} && slice(s, 20, 26) == range(0, 6) &&
                  s.size() == 27 && s[26] == -1,
              "arrange: блоки — повтор, обрыв, тишина, стоп");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 32));
        const auto out = run(*start(p, 24), 20);
        const auto c = cursor(out);
        check(!c.empty() && c.front() == 24 && stepsOf(out, 0).front() == 8 && has(out, SeqOut::Kind::ended) &&
                  c.back() == 32,
              "arrange: с курсора, стоп на конце");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 4, 10));
        check(slice(stepsOf(run(*start(p), 4), 0), 0, 4) == std::vector<int>{10, 11, 12, 13}, "arrange: offset");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 64));
        p.setLoop(4, 8);
        p.arrange.loop = true;
        const auto out = run(*start(p), 14);
        check(slice(cursor(out), 0, 14) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5} &&
                  slice(stepsOf(out, 0), 0, 12) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7} &&
                  !has(out, SeqOut::Kind::ended),
              "arrange: петля");
    }
    {
        Project p = project();
        p.tracks[1].clips[0] = Clip();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 64));
        p.tracks[1].placeBlock(ArrBlock(0, 0, 64));
        auto core = start(p);
        run(*core, 4);
        core->events.push(CoreEvent::launch(0, 1));
        auto out = run(*core, 20);
        const bool left = has(out, SeqOut::Kind::follow, 0, 0) && !has(out, SeqOut::Kind::follow, 1, 0) &&
                          clipsOf(out, 0) == std::vector<std::vector<int>>{{1, 0, 16}};
        core->events.push(CoreEvent::returnToArrange(-1));
        out = run(*core, 2);
        check(left && has(out, SeqOut::Kind::follow, 0, 1) && !clipsOf(out, 0).empty() && clipsOf(out, 0)[0][0] == 0 &&
                  stepsOf(out, 0).front() == 8,
              "arrange: клип из сетки уводит с таймлайна, возврат");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(1, 0, 4));
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        const auto out = run(core, 20);
        check(slice(stepsOf(out, 0), 0, 16) == range(0, 16) && !has(out, SeqOut::Kind::ended) &&
                  slice(cursor(out), 0, 3) == std::vector<int>{0, 1, 2},
              "arrange: сетка не слушает таймлайн, курсор идёт");
    }
    {
        Project p = project();
        p.setLoop(4, 8);
        p.arrange.loop = true;
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        check(slice(cursor(run(core, 12)), 0, 12) == range(0, 12), "arrange: игра из сетки — без петли");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 8));
        auto core = start(p);
        run(*core, 3);
        core->events.push(CoreEvent::make(CoreEvent::Kind::leaveArrange));
        const auto out = run(*core, 16);
        check(has(out, SeqOut::Kind::follow, 0, 0) && !has(out, SeqOut::Kind::ended) &&
                  slice(stepsOf(out, 0), 0, 6) == std::vector<int>{3, 4, 5, 6, 7, 8},
              "arrange: выход из аранжировки — клипы играют");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 64));
        auto core = start(p, 20);
        run(*core, 3);
        core->events.push(CoreEvent::seekArrange(0));
        const auto out = run(*core, 3);
        check(slice(cursor(out), 0, 3) == std::vector<int>{0, 1, 2} && slice(stepsOf(out, 0), 0, 3) == std::vector<int>{0, 1, 2},
              "arrange: перенос курсора на ходу");
    }
}

void automation() {
    const int rv = VoiceParam::reverb;
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 64));
        p.tracks[0].setAuto("reverb", {{0, 0}, {32, 1}});
        p.tracks[0].setAuto("prob", {{0, 0}, {64, 0}});
        auto core = start(p);
        run(*core, 16);
        const bool mid = near(param(*core, rv), 50, 2);
        const auto out = run(*core, 8);
        const bool noTrig = !has(out, SeqOut::Kind::trig, 0);
        core->events.push(CoreEvent::autoHold(0, rv, true));
        run(*core, 1);
        const bool hand = param(*core, rv) == 0;
        core->events.push(CoreEvent::autoHold(0, rv, false));
        run(*core, 1);
        const bool back = param(*core, rv) > 70;
        core->events.push(CoreEvent::play(false));
        run(*core, 1);
        check(mid && noTrig && hand && back && param(*core, rv) == 0, "arrange auto: кривая вместо ручки, рука, стоп");
    }
    {
        Project p = project();
        p.tracks[0].placeBlock(ArrBlock(0, 0, 64));
        p.tracks[0].setAuto("reverb", {{0, 1}, {16, 1}, {16, 0.5}, {24, 0.5}});
        auto core = start(p);
        std::vector<float> l(256), r(256);
        bool seen = false;
        for (int n = 0; !seen && n < 4000; ++n) {
            core->render(256, l.data(), r.data());
            while (auto e = core->sequencer.out.pop())
                if (e->kind == SeqOut::Kind::arrange && e->a == 16) seen = true;
        }
        const bool stepOk = seen && near(param(*core, rv), 50, 0.5);
        run(*core, 10);
        check(stepOk && param(*core, rv) == 0, "arrange auto: ступенька до трига, за концом — ручка");
    }
    {
        Project p = project();
        p.tracks[0].setAuto("reverb", {{0, 1}, {32, 1}});
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        run(core, 4);
        check(param(core, rv) == 0, "arrange auto: в сетке кривая не действует");
    }
    {
        ArrAutoLane l;
        l.param = "filter";
        l.points = {{4, 0.2}, {8, 1}};
        Track t(0);
        t.placeBlock(ArrBlock(0, 0, 16));
        t.setAuto("filter", {{8, 2}, {1, -1}});
        const bool pts = t.autoPoints("filter") == std::vector<ArrAutoPoint>{{1, 0}, {8, 1}};
        t.setAuto("nope", {});
        check(!l.value(3.9) && near(*l.value(6), 0.6, 1e-9) && !l.value(8.1) && pts && t.autos.size() == 1,
              "arrange auto: значение кривой, точки");
    }
    {
        std::vector<ArrAutoPoint> line;
        for (int i = 0; i <= 20; ++i) line.push_back({double(i), double(i) / 20});
        const std::vector<ArrAutoPoint> bend{{0, 0}, {1, 1}, {2, 0}};
        Track t(0);
        t.placeBlock(ArrBlock(0, 0, 64));
        t.setAuto("filter", {{0, 0.5}, {8, 0.5}, {32, 0.5}});
        std::vector<ArrAutoPoint> take;
        for (const auto& q : line) take.push_back({q.x + 4, q.y});
        t.recordAuto("filter", take);
        std::vector<double> xs;
        for (const auto& q : t.autoPoints("filter")) xs.push_back(q.x);
        t.recordAuto("pan", {{1, 0.2}, {2, 0.3}});
        check(ArrAutoLane::simplify(line, 0.01).size() == 2 && ArrAutoLane::simplify(bend, 0.01).size() == 3 &&
                  xs == std::vector<double>{0, 4, 24, 32} && t.autos.size() == 2,
              "arrange auto: запись ручки, упрощение");
    }
}

// MARK: — ArrangeOwnTests и соседи

std::vector<int> trigSteps(const Out& out) {
    int cur = -1;
    std::vector<int> got;
    for (const auto& e : out) {
        if (e.kind == SeqOut::Kind::step && e.track == 0) cur = e.a;
        if (e.kind == SeqOut::Kind::trig && e.track == 0) got.push_back(cur);
    }
    return got;
}

void own() {
    {
        Project p;
        for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0].reset();
        Clip c;
        c.pages[0][0].on = true;
        c.pages[0][8].on = true;
        p.tracks[0].clips[0] = c;
        p.tracks[0].placeBlock(ArrBlock(0, 0, 16));
        const bool first = p.ownArrPatterns(), second = p.ownArrPatterns();
        const bool same = p.tracks[0].blocks[0].pattern == c;
        p.tracks[0].clips[0]->pages[0][0].on = false;
        p.tracks[0].clips[0]->pages[0][4].on = true;
        const auto out = run(*start(p), 15);
        bool cell = false;
        for (const auto& e : out)
            if (e.kind == SeqOut::Kind::clip && e.track == 0 && e.a == SeqData::slots) cell = true;
        check(first && !second && same && trigSteps(out) == std::vector<int>{0, 8} && cell,
              "arrange own: своя копия клипа, сетка не меняет");
    }
    {
        Track t(0);
        Clip c;
        c.pages[0][3].on = true;
        t.blocks = {ArrBlock(0, 0, 32, 0, c)};
        t.splitArrange(16);
        Clip d;
        d.pages[0][5].on = true;
        const ArrBlock b(2, 16, 16, 0, d);
        ArrBlock back;
        check(t.blocks.size() == 2 && t.blocks[1].pattern == c && json::get(json::put(b), back) && back == b,
              "arrange own: split и файл сохраняют копию");
    }
    {
        Track t(0);
        t.blocks = {ArrBlock(0, 0, 16, 0, Clip())};
        t.editBlock = 0;
        t.toggleStep(3, {36});
        bool ok = t.blocks[0].pattern->pages[0][3].on && !t.clips[0]->pages[0][3].on && t.pages()[0][3].on;
        t.editBlock.reset();
        ok = ok && !t.pages()[0][3].on;
        t.editBlock = 5;
        ok = ok && !t.pages()[0][3].on;
        Track back;
        check(ok && json::get(json::put(t), back) && !back.editBlock, "arrange: STEPS правит копию блока");
    }
    {
        Project p;
        for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0].reset();
        Clip c;
        c.pages[0][0].on = true;
        c.pages[0][2].on = true;
        c.pages[0][2].locks["prob"] = 0;
        p.tracks[0].clips[0] = c;
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        std::vector<int> got;
        for (const auto& e : run(core, 4))
            if (e.kind == SeqOut::Kind::fired && e.track == 0) got.push_back(e.a);
        check(got == std::vector<int>{0}, "fired: только прошедшие Chance");
    }
}

using Played = std::vector<std::map<int, int>>;
using Fired = std::vector<std::set<int>>;

void fired() {
    {
        Project p;
        Clip c;
        c.pages = {emptyPage(), emptyPage()};
        c.length = 32;
        c.playFrom = 0;
        c.playTo = 0;
        c.pages[0][0].on = true;
        c.pages[0][4].on = true;
        c.pages[0][4].locks["prob"] = 50;
        c.pages[0][4].locks["filter"] = 40;
        c.pages[0][4].notes = {50};
        c.pages[0][4].sound = "kick2";
        c.pages[0][6].locks["filter"] = 20;
        c.pages[1][0].on = true;
        p.tracks[0].clips[0] = c;
        Played pp(static_cast<size_t>(Params::maxTracks));
        Fired ff(static_cast<size_t>(Params::maxTracks));
        for (int x = 0; x < 16; ++x) pp[0][x] = x;
        for (int x = 16; x < 20; ++x) pp[0][x] = 16 + (x - 16);
        ff[0] = {0, 16};
        const ArrRecorder::Seg seg(0, 0, 0, 20, 0);
        p.applyRecording({seg}, pp, ff);
        std::vector<int> lens;
        for (const auto& b : p.tracks[0].blocks) lens.push_back(b.length);
        bool ok = lens == std::vector<int>{16, 4} && p.tracks[0].blocks[0].pattern;
        if (ok) {
            const Clip& o = *p.tracks[0].blocks[0].pattern;
            ok = o.length == 16 && o.pages[0][0].on && o.pages[0][4] == Step() && o.pages[0][6].locks.at("filter") == 20 &&
                 p.tracks[0].blocks[1].pattern->pages[0][0].on;
        }
        Clip c2 = c;
        c2.pages[0][0].locks["prob"] = 30;
        p.tracks[0].clips[0] = c2;
        p.tracks[0].blocks.clear();
        p.applyRecording({seg}, pp, ff);
        check(ok && p.tracks[0].blocks[0].pattern->pages[0][0].locks.at("prob") == 100, "arrange fired: шаги по факту");
    }
    {
        Project p;
        Clip c;
        c.length = 16;
        c.pages[0][0].on = true;
        c.pages[0][0].notes = {48};
        p.tracks[0].clips[0] = c;
        ArrRecorder rec;
        std::vector<ArrRecorder::State> states{std::make_pair(0, 0), std::make_pair(-1, 0)};
        states.resize(size_t(Params::maxTracks));
        rec.begin(0, states);
        Step on4;
        on4.on = true;
        on4.notes = {50};
        for (int x = 0; x < 32; ++x) {
            rec.step(x);
            const int pos = x % 16;
            Step st = c.pages[0][size_t(pos)];
            if (x >= 16 && pos == 4) st = on4;
            if (x >= 16 && pos == 0) st = Step();
            rec.notePlayed(0, pos, &st);
            if (st.on) rec.noteFired(0);
        }
        const auto a = rec.liveNote(0, 8, {60}, 0.5);
        rec.liveLength(*a, 3);
        rec.liveNote(0, 0, {55}, std::nullopt);
        rec.liveNote(1, 20, {40}, std::nullopt);
        const bool none = !rec.liveNote(2, 3, {40}, std::nullopt);
        const auto segs = rec.finish();
        p.applyRecording(segs, rec.played(), rec.fired(), rec.heard(), rec.live());
        const Clip& b0 = *p.tracks[0].blocks[0].pattern;
        const Clip& b1 = *p.tracks[0].blocks[1].pattern;
        std::vector<std::vector<int>> t1;
        for (const auto& b : p.tracks[1].blocks) t1.push_back({b.start, b.length});
        check(none && b0.pages[0][0].notes == std::vector<int>{48, 55} && !b0.pages[0][4].on && b0.pages[0][8].on &&
                  b0.pages[0][8].notes == std::vector<int>{60} && b0.pages[0][8].len == 3 &&
                  near(b0.pages[0][8].noteVel(0).value_or(-1), 64, 0.5) && !b1.pages[0][0].on &&
                  b1.pages[0][4].notes == std::vector<int>{50} && t1 == std::vector<std::vector<int>>{{16, 16}} &&
                  p.tracks[1].blocks[0].pattern->pages[0][4].notes == std::vector<int>{40},
              "arrange fired: шаги как прозвучали, живые ноты");
    }
    {
        ArrRecorder rec;
        std::vector<ArrRecorder::State> states{std::make_pair(0, 0)};
        states.resize(size_t(Params::maxTracks));
        const bool empty = rec.current().empty();
        rec.begin(4, states);
        rec.step(5);
        rec.step(6);
        const auto a = rec.current();
        const bool first = a.size() == 1 && a[0].track == 0 && a[0].start == 4 && a[0].end == 7;
        rec.clip(0, 1);
        rec.step(7);
        const auto b = rec.current();
        check(empty && first && b.size() == 2 && b[0].slot == 0 && b[0].start == 4 && b[0].end == 6 && b[1].slot == 1 &&
                  b[1].start == 6 && b[1].end == 8,
              "arrange rec: идущая запись");
    }
    {
        Project p;
        Clip c;
        c.pages[0][0].on = true;
        p.tracks[0].clips[0] = c;
        Played pp(static_cast<size_t>(Params::maxTracks));
        for (int x = 0; x < 300; ++x) pp[0][x] = x % 16;
        std::set<int> beats;
        for (int x = 0; x < 300; x += 16) beats.insert(x);
        const Fired ff(size_t(Params::maxTracks), beats);
        p.applyRecording({ArrRecorder::Seg(0, 0, 0, 300, 0)}, pp, ff);
        const auto& bl = p.tracks[0].blocks;
        check(bl.size() == 19 && bl.back().length == 12 && bl[1].start == 16 && bl[18].pattern->pages[0][0].on,
              "arrange fired: длинный — блоками по проходу");
    }
    {
        Project p;
        Clip c;
        c.pages = {emptyPage(), emptyPage()};
        c.length = 32;
        c.playTo = 1;
        c.pages[0][0].on = true;
        p.tracks[0].clips[0] = c;
        p.applyRecording({ArrRecorder::Seg(0, 0, 0, 72, 8)});
        std::vector<std::vector<int>> v;
        for (const auto& b : p.tracks[0].blocks) v.push_back({b.start, b.length, b.offset});
        Track t(0);
        Clip big;
        big.pages = std::vector<Page>(8, emptyPage());
        big.length = 128;
        big.playTo = 7;
        big.pages[0][0].on = true;
        t.clips[0] = big;
        t.addBlock(0, 0, 300);
        std::vector<int> lens;
        for (const auto& b : t.blocks) lens.push_back(b.length);
        check(v == std::vector<std::vector<int>>{{24, 32, 0}, {56, 16, 0}} && lens == std::vector<int>{128, 128, 44},
              "arrange: блоки по длине паттерна, не длиннее 8 тактов");
    }
    {
        Project p;
        Clip c;
        c.pages = {emptyPage(), emptyPage()};
        c.length = 32;
        c.playTo = 1;
        c.pages[0][0].on = true;
        p.tracks[0].clips[0] = c;
        p.applyRecording({ArrRecorder::Seg(0, 0, 0, 64, 16)});
        std::vector<std::vector<int>> v;
        for (const auto& b : p.tracks[0].blocks) v.push_back({b.start, b.length});
        Project q;
        Clip d;
        d.pages[0][0].on = true;
        q.tracks[0].clips[0] = d;
        Played pp(static_cast<size_t>(Params::maxTracks));
        for (int x = 0; x < 48; ++x) pp[0][x] = x % 16;
        Fired ff(static_cast<size_t>(Params::maxTracks));
        ff[0] = {0, 32};
        q.applyRecording({ArrRecorder::Seg(0, 0, 0, 48, 0)}, pp, ff);
        std::vector<int> starts;
        for (const auto& b : q.tracks[0].blocks) starts.push_back(b.start);
        check(v == std::vector<std::vector<int>>{{16, 32}, {48, 16}} && starts == std::vector<int>{0, 32},
              "arrange: пустые участки не пишутся");
    }
}

// MARK: — ArrangeAutoReturnTests

void autoReturn(bool ownCopy) {
    Project p;
    for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0].reset();
    p.tracks[0].clips[0]->pages[0][0].on = true;
    p.tracks[0].clips[0]->pages[0][8].on = true;
    p.tracks[0].params["filter"] = 60;
    const Clip c = *p.tracks[0].clips[0];
    for (int k = 0; k < 6; ++k)
        p.tracks[0].placeBlock(ArrBlock(0, k * 16, 16, 0, ownCopy ? std::optional<Clip>(c) : std::nullopt));
    p.tracks[0].setAuto("filter", {{16, 0.8}, {32, 0.8}});
    auto core = start(p);
    // Как интерфейс во время игры: снимок звука публикуется заново (каждые ~0,1 с).
    const auto again = [&](int pos) {
        if ((pos / 256) % 20 == 0) core->publish(p);
    };
    const int f = VoiceParam::filter;
    run(*core, 8, again);
    const float knob = param(*core, f);
    run(*core, 16, again);
    const float mid = param(*core, f);
    run(*core, 20, again);
    check(mid != knob && std::fabs(param(*core, f) - knob) < 1e-4f,
          std::string("arrange auto: после кривой — ручка") + (ownCopy ? " (копии)" : ""));
}

std::vector<float> renderLeft(AudioCore& core, int frames) {
    std::vector<float> l(256), r(256), y;
    while (int(y.size()) < frames) {
        core.render(256, l.data(), r.data());
        while (core.sequencer.out.pop()) {}
        y.insert(y.end(), l.begin(), l.end());
    }
    return y;
}

Project kick(int type, double knob, double decay) {
    Project p;
    for (int t = 1; t < 8; ++t) p.tracks[size_t(t)].clips[0].reset();
    auto& t = p.tracks[0];
    t.setSynth(16);
    t.params[Params::filterTypeId] = double(type);
    t.params["filter"] = knob;
    t.params["decay"] = decay;
    t.params["attack"] = 0;
    return p;
}

void synthReturns() {
    const auto energy = [](bool curve) {
        Project p = kick(FilterType::hp, 0, 1000);
        for (int k = 0; k < 16; k += 2) {
            p.tracks[0].clips[0]->pages[0][size_t(k)].on = true;
            p.tracks[0].clips[0]->pages[0][size_t(k)].notes = {48};
        }
        p.tracks[0].placeBlock(ArrBlock(0, 16, 16));
        p.tracks[0].placeBlock(ArrBlock(0, 32, 16));
        if (curve) p.tracks[0].setAuto("filter", {{16, 0.9}, {32, 0.9}});
        auto core = start(p, 0, false, true);
        std::vector<float> l(256), r(256);
        double e = 0;
        for (int pos = 0; pos < 46 * stepF; pos += 256) {
            core->render(256, l.data(), r.data());
            while (core->sequencer.out.pop()) {}
            if (pos >= 36 * stepF)
                for (float x : l) e += double(x * x);
        }
        return e;
    };
    const double plain = energy(false), withAuto = energy(true);
    check(plain > 0 && near(withAuto / plain, 1, 0.02), "arrange auto: KICK после кривой — как без неё");
}

void filterEnd() {
    int lastType = 99;
    const double hpY = FilterMigration::value(40, FilterType::hp) / 100;
    const auto render = [&](bool curve, bool fromOff, bool lpEnd) {
        Project p = kick(lpEnd ? FilterType::lp : FilterType::hp, lpEnd ? 100 : 0, 1000);
        p.tracks[0].clips[0]->pages[0][8].on = true;
        p.tracks[0].clips[0]->pages[0][8].notes = {48};
        p.tracks[0].placeBlock(ArrBlock(0, 0, 16));
        if (curve) {
            std::vector<ArrAutoPoint> pts{{0, hpY}, {9, hpY}};
            if (fromOff) pts = {{9, hpY}, {16, hpY}};
            if (lpEnd) {
                const double y = FilterMigration::value(-60, FilterType::lp) / 100;
                pts = {{0, y}, {9, y}};
            }
            p.tracks[0].setAuto("filter", pts);
        }
        auto core = start(p, 0, false, true);
        const auto y = renderLeft(*core, 12 * stepF);
        if (const auto* sv = core->sampler.synth)
            for (int v = 0; v < SynthVoices::slots; ++v)
                if (sv->isActive(v)) lastType = sv->voiceFilterType(v);
        return y;
    };
    const auto jump = [](const std::vector<float>& y, int a, int b) {
        float m = 0;
        for (int i = a + 1; i < b; ++i) m = std::max(m, std::fabs(y[size_t(i)] - y[size_t(i - 1)]));
        return m;
    };
    const int a = 9 * stepF - 2000, b = 9 * stepF + 4000;
    const float ref = jump(render(false, false, false), a, b);
    const float atEnd = jump(render(true, false, false), a, b);
    std::printf("FILTER END: jump %.5f, plain %.5f\n", atEnd, ref);
    const float on = jump(render(true, true, false), a, b);
    const int typeAfter = lastType;
    const float le = jump(render(true, false, true), a, b);
    const float ref2 = jump(render(false, false, true), a, b);
    std::printf("FILTER OFF->HP: jump %.5f; LP END: jump %.5f, plain %.5f\n", on, le, ref2);
    check(ref > 0 && atEnd <= ref * 1.1f && on <= ref * 1.1f && typeAfter == 1 && le <= ref2 * 1.1f,
          "arrange auto: конец кривой фильтра — без скачка");
}

void noBurst() {
    const auto render = [](bool curve, double reso) {
        Project p = kick(FilterType::hp, 0, 600);
        p.tracks[0].params["reso"] = reso;
        for (int k : {0, 4, 8, 12}) {
            p.tracks[0].clips[0]->pages[0][size_t(k)].on = true;
            p.tracks[0].clips[0]->pages[0][size_t(k)].notes = {48};
        }
        p.tracks[0].placeBlock(ArrBlock(0, 0, 16));
        const double y = FilterMigration::value(80, FilterType::hp) / 100;
        if (curve) p.tracks[0].setAuto("filter", {{0, y}, {8, y}});
        auto core = start(p, 0, false, true);
        return renderLeft(*core, 14 * stepF);
    };
    bool ok = true;
    for (double reso : {0.0, 50.0}) {
        const auto a = render(true, reso), b = render(false, reso);
        for (int w = 0; w < 12; ++w) {
            const int i0 = 8 * stepF + w * 500;
            float pa = 0, pb = 0;
            for (int i = i0; i < i0 + 500; ++i) {
                pa = std::max(pa, std::fabs(a[size_t(i)]));
                pb = std::max(pb, std::fabs(b[size_t(i)]));
            }
            ok = ok && pa <= pb * 1.1f + 0.002f;
        }
    }
    check(ok, "arrange auto: после кривой HP — без всплеска");
}

void hpToOff() {
    const auto render = [](const std::vector<ArrAutoPoint>* pts) {
        Project p = kick(FilterType::hp, 0, 1000);
        p.tracks[0].clips[0]->pages[0][9].on = true;
        p.tracks[0].clips[0]->pages[0][9].notes = {36};
        p.tracks[0].placeBlock(ArrBlock(0, 0, 16));
        if (pts) p.tracks[0].setAuto("filter", *pts);
        auto core = start(p, 0, false, true);
        return renderLeft(*core, 14 * stepF);
    };
    const auto highPeak = [](const std::vector<float>& y, int a, int b) {
        Biquad hp;
        hp.set(1, 1000, 0, 48000);
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0, m = 0;
        for (int i = 0; i < b; ++i) {
            const float o = hp.b0 * y[size_t(i)] + hp.b1 * x1 + hp.b2 * x2 - hp.a1 * y1 - hp.a2 * y2;
            x2 = x1;
            x1 = y[size_t(i)];
            y2 = y1;
            y1 = o;
            if (i >= a) m = std::max(m, std::fabs(o));
        }
        return m;
    };
    const auto rms = [](const std::vector<float>& y, int i0) {
        float e = 0;
        for (int i = i0; i < i0 + 240; ++i) e += y[size_t(i)] * y[size_t(i)];
        return std::sqrt(e / 240);
    };
    const double hpY = FilterMigration::value(50, FilterType::hp) / 100;
    const auto off = render(nullptr);
    const std::vector<ArrAutoPoint> full{{0, hpY}, {16, hpY}};
    const auto hp = render(&full);
    bool ok = true;
    for (double at : {11.0, 11.4, 12.0}) {
        const std::vector<ArrAutoPoint> pts{{0, hpY}, {at, hpY}, {at, 0}, {16, 0}};
        const auto stepped = render(&pts);
        const int a = int(at * stepF) - 500, b = int(at * stepF) + 1500;
        const float s = highPeak(stepped, a, b), r = std::max(highPeak(off, a, b), highPeak(hp, a, b));
        float lo = 0;
        for (int i = a + 6500; i < a + 7500; ++i) lo = std::max(lo, std::fabs(stepped[size_t(i)] - off[size_t(i)]));
        const int s0 = int(at * stepF);
        const float early = rms(stepped, s0 + 240) / std::max(1e-9f, rms(off, s0 + 240));
        const float late = rms(stepped, s0 + 7200) / std::max(1e-9f, rms(off, s0 + 7200));
        std::printf("HP->OFF at %.1f: high %.6f, ref %.6f, low early %.3f, late %.3f\n", at, s, r, early, late);
        ok = ok && r > 0 && s <= r * 1.1f && lo < 1e-3f && early < 0.33f && std::fabs(late - 1) < 0.02f;
    }
    check(ok, "arrange auto: ступенька HP → выкл. — без щелчка и толчка");
}
}  // namespace

int runArrangeTests() {
    play();
    automation();
    own();
    fired();
    autoReturn(false);
    autoReturn(true);
    synthReturns();
    filterEnd();
    noBurst();
    hpToOff();
    std::printf("arrange: %d / %d passed\n", checks - fails, checks);
    return fails;
}
