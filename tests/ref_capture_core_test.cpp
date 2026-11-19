// Порт RefTests (референс аранжировщика) и CaptureTests (REC в редакторе сэмпла) через ядро (ветка swift JAMbo).
// Без CaptureTests.testRecordedSampleOnTrack — правки папки дорожки (Sounds.swift) ещё не перенесены.
#include "engine/AudioCore.h"
#include "engine/Capture.h"
#include "engine/StemRing.h"
#include "model/Json.h"
#include "model/Project.h"
#include <cmath>
#include <cstdio>
#include <functional>
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

// MARK: — Ref

const float unit = 1e-6f;

/// Сэмпл-рампа: значение кадра i — i·unit.
SampleRef ramp(int frames) {
    std::vector<float> l(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) l[size_t(i)] = float(i) * unit;
    return std::make_shared<SampleData>(l, &l);
}

Project refProject(double offset = 0) {
    Project p;
    for (auto& t : p.tracks)
        for (auto& c : t.clips) c.reset();
    ArrRef r;
    r.key = "r";
    r.name = "ref.wav";
    r.offset = offset;
    p.arrange.ref = r;
    return p;
}

void publishRef(AudioCore& c, const Project& p, bool b) {
    SoundSnapshot::Options o;
    o.refB = b;
    c.publish(p, {{"r", ramp(400000)}}, o);
}

std::unique_ptr<AudioCore> refCore(const Project& p, bool b, bool arrange = true, int from = 0) {
    auto c = std::make_unique<AudioCore>();
    publishRef(*c, p, b);
    if (arrange) c->events.push(CoreEvent::arrange(true, from));
    c->events.push(CoreEvent::play(true));
    return c;
}

/// Блоки по 256: левый канал последнего блока.
std::vector<float> blocks(AudioCore& c, int n) {
    std::vector<float> l(256), r(256);
    for (int i = 0; i < n; ++i) c.render(256, l.data(), r.data());
    return l;
}

int at(float v) { return int(std::lround(v / unit)); }
float absMax(const std::vector<float>& x) {
    float m = 0;
    for (float v : x) m = std::max(m, std::fabs(v));
    return m;
}

void ref() {
    {
        auto c = refCore(refProject(), true);
        const auto l = blocks(*c, 20);
        check(std::abs(at(l[0]) - 19 * 256) <= 3 && at(l[255]) - at(l[0]) == 255 && c->ref.active,
              "ref: B — по курсору, подряд, в своём темпе");
    }
    {
        Project p = refProject();
        p.outVol = -30;
        auto c = refCore(p, true);
        const auto l = blocks(*c, 20);
        float peak = 0;
        c->levels.read(c->levels.blocks.load() - 1, [&](const float* b) { peak = Levels::value(b, Levels::ref, 0); });
        check(std::abs(at(l[0]) - 19 * 256) <= 3 && std::fabs(l[255] - l[0] - 255 * unit) < 1e-7f &&
                  std::fabs(peak - l[255]) < 1e-7f,
              "ref: мимо мастера, метр — пик файла");
    }
    {
        auto c = refCore(refProject(0.5), true, true, 16);
        const auto l = blocks(*c, 10);
        check(std::abs(at(l[0]) - (72000 + 9 * 256)) <= 3, "ref: курсор и сдвиг начала файла");
    }
    {
        auto c = refCore(refProject(1), true);
        check(absMax(blocks(*c, 10)) == 0, "ref: до начала файла — тишина");
    }
    {
        auto c = refCore(refProject(), false);
        const auto l = blocks(*c, 20);
        float peak = 0;
        c->levels.read(0, [&](const float* b) { peak = std::max(peak, Levels::value(b, Levels::ref, 0)); });
        check(absMax(l) < 1e-6f && peak > 0, "ref: A — слышен проект, метр референса идёт");
    }
    {
        auto c = refCore(refProject(), false);
        blocks(*c, 20);
        Project p = refProject();
        p.bpm = c->sequencer.bpm();
        publishRef(*c, p, true);
        const auto l = blocks(*c, 1);
        const bool smooth = l[0] < l[255] * 0.1f && std::fabs(c->ref.ab - 256.f / 960.f) < 0.01f;
        const auto l2 = blocks(*c, 3);
        check(smooth && std::abs(at(l2[255]) - (23 * 256 + 255)) <= 3, "ref: переход A/B — плавно, 20 мс");
    }
    {
        auto c = refCore(refProject(), true, false);
        check(absMax(blocks(*c, 20)) == 0 && !c->ref.active, "ref: только с таймлайна");
    }
    {
        auto c = refCore(refProject(), true);
        blocks(*c, 20);
        c->events.push(CoreEvent::play(false));
        const auto l = blocks(*c, 1);
        const bool fade = l[0] > 0 && std::fabs(l[255]) < l[0] * 0.05f && !c->ref.active;
        check(fade && absMax(blocks(*c, 1)) == 0, "ref: стоп — затухание за блок");
    }
    {
        Project p = refProject();
        p.arrange.loopFrom = 0;
        p.arrange.loopTo = 16;
        p.arrange.loop = true;
        auto c = refCore(p, true);
        const auto l = blocks(*c, 400);
        check(std::abs(at(l[0]) - (399 * 256 - 96000)) <= 3, "ref: петля — снова с начала");
    }
    {
        Project p = refProject(1.25);
        p.arrange.ref->gain = -6;
        p.arrange.ref->bpm = 126;
        p.arrange.showRef = true;
        Project back;
        const bool round = json::get(json::put(p), back) && back.arrange.ref == p.arrange.ref && back.arrange.showRef;
        Arrangement a;
        ArrRef x;
        x.key = "x";
        x.gain = 12;
        a.ref = x;
        a.repair();
        bool ok = a.ref && a.ref->gain == 0;
        a.ref->bpm = -INFINITY;
        a.repair();
        ok = ok && !a.ref->bpm;
        a.ref = ArrRef();
        a.repair();
        check(p.sampleKeys().count("r") && round && ok && !a.ref, "ref: данные, ключ, repair");
    }
}

// MARK: — Capture

using Done = std::map<int, SampleRef>;

void renderCap(AudioCore& core, StemCollector& col, int frames, std::vector<float>& out, Done& done,
               const std::function<void(int)>& between) {
    std::vector<float> bl(256), br(256);
    for (int pos = 0; pos < frames;) {
        between(pos);
        const int n = std::min(256, frames - pos);
        core.render(n, bl.data(), br.data());
        out.insert(out.end(), bl.begin(), bl.begin() + n);
        for (auto& s : col.poll(core.sequencer.stemRing)) done[s.channel] = s.data;
        pos += n;
    }
}

std::unique_ptr<AudioCore> capCore(bool master) {
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].vol = 0;
    auto& s = p.tracks[0].pagesMut()[0][0];
    s.on = true;
    s.notes = {48};
    s.len = 16;
    auto c = std::make_unique<AudioCore>(master);
    c->publish(p, {{"k", std::make_shared<SampleData>(std::vector<float>(size_t(48000 * 4), 0.5f), nullptr)}});
    c->sequencer.stemRing.prepare();
    c->events.push(CoreEvent::play(true));
    return c;
}

void capture() {
    {
        auto c = capCore(false);
        StemCollector col;
        std::vector<float> out;
        Done done;
        renderCap(*c, col, 48000, out, done, [&](int pos) {
            if (pos == 2560) c->events.push(CoreEvent::captureStart(0));
            if (pos == 2560 + 25600) c->events.push(CoreEvent::make(CoreEvent::Kind::captureStop));
        });
        bool ok = done.count(StemRing::capture) && done[StemRing::capture]->frames() == 25600 && !done.count(0);
        if (ok) {
            const auto& d = *done[StemRing::capture];
            for (int i = 0; i < 25600; i += 997) ok = ok && std::fabs(d.left()[i] - out[size_t(2560 + i)]) < 1e-6f;
            ok = ok && std::fabs(d.left()[12800]) > 0.1f;
        }
        check(ok, "capture: дорожка — от старта до стопа, как выход");
    }
    {
        auto c = capCore(true);
        StemCollector col;
        std::vector<float> out;
        Done done;
        renderCap(*c, col, 24000, out, done, [&](int pos) {
            if (pos == 0) c->events.push(CoreEvent::captureStart(StemRing::master));
            if (pos == 12800) c->events.push(CoreEvent::make(CoreEvent::Kind::captureStop));
        });
        bool ok = done.count(StemRing::capture) && done[StemRing::capture]->frames() == 12800;
        if (ok)
            for (int i = 0; i < 12800; i += 501)
                ok = ok && std::fabs(done[StemRing::capture]->left()[i] - out[size_t(i)]) < 1e-6f;
        check(ok, "capture: мастер — конечный выход");
    }
    {
        StemRing ring;
        ring.prepare();
        ring.begin(StemRing::capture, 100, 300);
        const std::vector<float> l(256, 1.f);
        for (int64_t t = 0; t < 1024; t += 256) ring.captureBlock(StemRing::capture, l.data(), l.data(), nullptr, t, 256);
        StemCollector col;
        const auto done = col.poll(ring);
        check(done.size() == 1 && done[0].data->frames() == 300 && !ring.isActive(StemRing::capture),
              "capture: предел длины");
    }
    {
        RecPeaks p;
        std::vector<float> l(300, 0.f), r(300, 0.f);
        l[5] = 0.5f;
        l[130] = -0.25f;
        p.update(l, r);
        bool ok = p.count() == 2 && p.frames == 256 && p.hi[0][0] == 0.5f && p.lo[0][1] == -0.25f;
        l.insert(l.end(), 84, 0.1f);
        r.insert(r.end(), 84, 0.f);
        p.update(l, r);
        const auto rg = p.range(0, 0, 3);
        ok = ok && p.count() == 3 && rg.first == -0.25f && rg.second == 0.5f && p.span() == Capture::minSpan &&
             Capture::minSpan * Capture::peakBlock == 5 * 48000 && Capture::maxFrames == int64_t(Capture::maxSeconds) * 48000;
        check(ok, "capture: пики по блокам");
    }
    check(Capture::time(0) == "0:00" && Capture::time(12.9) == "0:12" && Capture::time(60) == "1:00", "capture: время");
}
}  // namespace

int runRefCaptureTests() {
    ref();
    capture();
    std::printf("ref/capture core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
