// Порт StepNotesTests, PianoTests (без testHistorySerialAndSettingsDecode: AppSettings — интерфейс) и RenderPoolTests
// со звуком ядра (ветка swift JAMbo). HeavyLoadTests.heavyProject — здесь же (для RenderPool).
#include "engine/AudioCore.h"
#include "eq/EqSnapshot.h"
#include "model/Json.h"
#include "model/Project.h"
#include "model/Recording.h"
#include "model/Scales.h"
#include "sequencer/SeqSnapshot.h"
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
bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

std::vector<float> render(AudioCore& core, int frames) {
    std::vector<float> l(static_cast<size_t>(frames)), br(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, l.data() + pos, br.data());
        pos += n;
    }
    return l;
}

const float center = float(std::cos(M_PI / 4));
const double quarter = 127 * std::pow(0.25, 1 / 1.5);

Project project() {
    Project p;
    p.tracks[0].sample = "k";
    p.tracks[0].vol = 0;
    return p;
}

std::map<std::string, SampleRef> dc() {
    return {{"k", std::make_shared<SampleData>(std::vector<float>(96000, 1.f), nullptr)}};
}

void stepNotes() {
    Step s;
    s.on = true;
    s.addNote(48, 2, std::nullopt);
    bool ok = s.notes == std::vector<int>{48} && s.len == 2 && !s.lens;
    s.addNote(43, 4, 90);
    ok = ok && s.notes == std::vector<int>{43, 48} && s.lens == std::vector<int>{4, 2} && s.len == 4 &&
         s.vels == std::vector<double>{90, -1} && s.noteVel(0) == 90.0 && !s.noteVel(1) && Track::hasManualLocks(s);
    s.setNoteVel(0, std::nullopt);
    ok = ok && !s.vels && !Track::hasManualLocks(s);
    s.setNotes({48, 50});
    ok = ok && s.lens == std::vector<int>{2, 4};
    s.removeNote(1);
    s.removeNote(0);
    check(ok && s.notes.empty() && !s.on, "step notes: добавить, убрать, нормализовать");

    Track t(0);
    t.withStep(2, [](Step& x) {
        x.on = true;
        x.addNote(48, 1, 50);
        x.addNote(52, 3, std::nullopt);
        x.setAllLen(2);
    });
    const Step& a = t.pages()[0][2];
    ok = !a.lens && a.noteLen(1) == 2 && a.vels == std::vector<double>{50, -1};
    t.clearLock(2, "level");
    check(ok && !t.pages()[0][2].vels, "step notes: длина всех, снять Level — и velocity");

    Step old;
    const bool got = json::get(Json::parse(R"({"on":true,"len":3,"notes":[48,52],"locks":{}})"), old);
    Step back;
    check(got && !old.lens && !old.vels && old.noteLen(1) == 3 && json::get(json::put(old), back) && back == old,
          "step notes: старый шаг читается");

    Project p = project();
    p.tracks[0].withStep(0, [](Step& x) {
        x.on = true;
        x.addNote(48, 2, 30);
        x.addNote(60, 1, std::nullopt);
    });
    const SeqSnapshot snap(p);
    const auto& d = snap.data();
    const int i = d.stepIndex(0, 0, 0);
    const int m = SeqData::maxNotes;
    check(d.split[size_t(i)] == 1 && d.len[size_t(i)] == 2 && d.noteLen[size_t(i * m)] == 2 &&
              d.noteLen[size_t(i * m + 1)] == 1 && d.noteVel[size_t(i * m)] == 30 && d.noteVel[size_t(i * m + 1)] == -1,
          "step notes: снимок несёт ноты");

    for (int variant = 0; variant < 2; ++variant) {
        AudioCore core(false);
        Project q = project();
        q.tracks[0].withStep(0, [&](Step& x) {
            x.on = true;
            x.addNote(48, variant == 0 ? 2 : 1, quarter);
            x.addNote(60, variant == 0 ? 1 : 2, 0);
        });
        core.publish(q, dc());
        core.events.push(CoreEvent::play(true));
        const auto o = render(core, 13000);
        if (variant == 0)
            check(near(o[100], center * 0.25, 0.005) && near(o[11900], center * 0.25, 0.005) &&
                      o[12999] < center * 0.25f * 0.995f,
                  "step notes: velocity и своя длина ноты");
        else
            check(o[11900] < center * 0.25f * 0.9f, "step notes: короткая нота в длинном шаге");
    }
}

void piano() {
    std::vector<int> notes;
    for (int k = 0; k < 8; ++k) notes.push_back(Scales::note(1, 0, 2, k));
    check(notes == std::vector<int>{36, 38, 39, 41, 43, 44, 46, 48} && Scales::keyCount(1, 2) == 15 &&
              Scales::keyCount(8, 1) == 6 && Scales::keyCount(10, 2) == 25 && Scales::note(0, 4, 2, 0) == 40,
          "piano: ноты гаммы и клавиши");
    check(Scales::chord(0, 0, 2, 0) == std::vector<int>{36, 40, 43} &&
              Scales::chord(0, 0, 2, 0, 1) == std::vector<int>{36, 40, 43, 47} &&
              Scales::chord(0, 0, 2, 4, 2) == std::vector<int>{43, 47, 50, 53, 57} &&
              Scales::chord(0, 0, 2, 0, 3) == std::vector<int>{36, 38, 43} &&
              Scales::chord(0, 0, 2, 0, 4) == std::vector<int>{36, 41, 43} &&
              Scales::chord(0, 0, 2, 0, 5) == std::vector<int>{36, 40, 43, 50} &&
              Scales::chordKinds.size() == Scales::chordDegrees.size(),
          "piano: аккорды");
    Track t(0);
    t.pagesMut()[0][3].on = true;
    t.pagesMut()[0][3].notes = {40};
    t.recordNotes(3, {36}, false, false, std::nullopt);
    bool ok = t.pages()[0][3].notes == std::vector<int>{36};
    t.recordNotes(3, {43}, true, false, std::nullopt);
    ok = ok && t.pages()[0][3].notes == std::vector<int>{36, 43};
    t.recordNotes(3, {48}, false, true, std::nullopt);
    ok = ok && t.pages()[0][3].notes == std::vector<int>{36, 43, 48};
    check(ok, "piano: запись — замена, аккорд в дубле, OVERDUB");
    t.recordNotes(5, {36}, false, false, 0.5);
    ok = t.pages()[0][5].vels == std::vector<double>{64} && t.pages()[0][5].locks.at("level") == 64;
    t.recordNotes(5, {40}, true, false, 0.9);
    ok = ok && t.pages()[0][5].vels == std::vector<double>{64, 114} && t.pages()[0][5].locks.at("level") == 114;
    t.recordNotes(5, {36}, true, false, 0.2);
    ok = ok && t.pages()[0][5].vels == std::vector<double>{64, 114};
    t.recordLength(5, 30);
    ok = ok && t.pages()[0][5].len == 11;
    const bool c1 = t.clearPassStep(5), c2 = t.clearPassStep(5);
    check(ok && c1 && !c2, "piano: сила нот, длина, очистка шага");
    check(RecordClock::tick(57500, 10, 60000, 6000, true) == 10 && RecordClock::tick(57500, 10, 60000, 6000, false) == 9 &&
              RecordClock::length(0.3, 0.125) == 2 && RecordClock::length(0.01, 0.125) == 1,
          "piano: шаг записи по времени");
    AudioCore core;
    core.publish(Project());
    core.events.push(CoreEvent::play(true));
    std::vector<float> l(256), r(256);
    for (int i = 0; i < 30; ++i) core.render(256, l.data(), r.data(), 1.5);
    const auto s = core.sequencer.clock.read();
    check(s.playing && near(s.stepFrames, 6000, 1e-9) && s.blockFrame == 29 * 256 && s.nextTick == 2 &&
              near(s.nextFrame, 12000, 1e-6) && s.blockHost == 1.5,
          "piano: часы игры из колбэка");
}

// MARK: — RenderPool со звуком ядра

Project heavyProject() {
    Project p;
    for (int t = 0; t < 8; ++t) {
        auto& tr = p.tracks[size_t(t)];
        if (t < 4) {
            tr.setSynth(t % 2 == 0 ? 17 : 18);
        } else {
            tr.sample = "s" + std::to_string(t);
            tr.params["decay"] = 2000;
            tr.params["filter"] = 30;
            tr.params["reso"] = 40;
        }
        tr.vol = -12;
        tr.params["reverb"] = 30;
        tr.params["delay"] = 25;
        tr.params["drive"] = 20;
        if (t != 4) tr.params["sidechain"] = 50;
        Page page = emptyPage();
        for (int s = 0; s < 16; ++s) {
            if (t < 4) {
                if (s % 4 != 0) continue;
                page[size_t(s)].on = true;
                page[size_t(s)].len = 4;
                page[size_t(s)].notes = {48 + t, 52 + t, 55 + t, 59 + t};
            } else {
                page[size_t(s)].on = true;
                page[size_t(s)].notes = {48 + s % 5};
            }
        }
        tr.pagesMut() = {page};
        p.addEqBand(t, 80, 0);
        const int bell = p.addEqBand(t, 400 + double(t) * 100, -3);
        p.setEqDyn(t, bell, (t + 1) % 8 + 1);
        p.setEqDynParams(t, bell, -40, -6);
        p.addEqBand(t, 8000, 2);
    }
    p.fx["sc.source"] = 4;
    const int m = EqSpec::masterChannel;
    p.addEqBand(m, 60, 1);
    const int mb = p.addEqBand(m, 250, -2);
    p.setEqDyn(m, mb, 5);
    p.setEqDynParams(m, mb, -30, -4);
    p.addEqBand(m, 12000, 1.5);
    p.master["sat.drive"] = 30;
    p.master["cmp.threshold"] = -20;
    p.master["clip.drive"] = 3;
    p.master["lim.input"] = 3;
    return p;
}

/// Стерео-сэмпл 2 с: шум с огибающей и синус (разный у дорожек).
SampleRef heavySample(int t) {
    uint32_t rng = 0x12345678u + uint32_t(t);
    std::vector<float> l(96000), r(96000);
    for (int i = 0; i < 96000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = float(int32_t(rng)) / float(INT32_MAX);
        const float env = float(std::exp(-double(i % 12000) / 3000));
        const float tone = float(std::sin(2 * M_PI * double(110 * (t - 2)) * double(i) / 48000));
        l[size_t(i)] = 0.4f * (noise * env + tone * 0.5f);
        r[size_t(i)] = 0.4f * (noise * env * 0.8f + tone * 0.5f);
    }
    return std::make_shared<SampleData>(l, &r);
}

std::unique_ptr<AudioCore> heavyCore(const Project& p) {
    auto core = std::make_unique<AudioCore>();
    core->installSynth();
    std::map<std::string, SampleRef> samples;
    for (int t = 4; t < 8; ++t) samples["s" + std::to_string(t)] = heavySample(t);
    core->publish(p, samples);
    core->events.push(CoreEvent::play(true));
    return core;
}

/// Шины дорожек и посылов с помощниками — те же, что без них (выход целиком не сравнить: у Reverb свой засев).
bool sameWithHelpers(const Project& project) {
    auto a = heavyCore(project), b = heavyCore(project);
    std::vector<float> bl(256), br(256);
    a->render(256, bl.data(), br.data());
    b->render(256, bl.data(), br.data());
    b->pool.start(3);
    float diff = 0, peak = 0;
    const int mf = 4096;
    for (int k = 0; k < 2 * 48000 / 256; ++k) {
        a->render(256, bl.data(), br.data());
        b->render(256, bl.data(), br.data());
        const auto& sa = a->sampler;
        const auto& sb = b->sampler;
        for (int t = 0; t < 8; ++t)
            for (int i = 0; i < 256; ++i) {
                const size_t j = size_t(t * mf + i);
                diff = std::max({diff, std::fabs(sa.trackBusL()[j] - sb.trackBusL()[j]), std::fabs(sa.trackBusR()[j] - sb.trackBusR()[j])});
                peak = std::max(peak, std::fabs(sa.trackBusL()[j]));
                for (int s = 0; s < 3; ++s) {
                    const size_t q = size_t((t * 3 + s) * mf + i);
                    diff = std::max({diff, std::fabs(sa.sendBusL()[q] - sb.sendBusL()[q]), std::fabs(sa.sendBusR()[q] - sb.sendBusR()[q])});
                }
            }
    }
    b->pool.stop();
    return diff == 0 && peak > 0.01f;
}

void renderPool() {
    check(sameWithHelpers(heavyProject()), "core pool: тяжёлый проект — звук тот же");
    Project p = heavyProject();
    bool alone = true;
    for (int t = 0; t < 8; ++t) {
        const int bell = p.addEqBand(t, 1200, -4);
        p.setEqDyn(t, bell, t + 1);
        p.setEqDynParams(t, bell, -30, -9);
        for (int k = 0; k < int(p.tracks[size_t(t)].eq.bands.size()); ++k) {
            const auto& b = p.tracks[size_t(t)].eq.bands[size_t(k)];
            if (b && b->dyn != t + 1) p.setEqDyn(t, k, 0);
        }
        alone = alone && !EqSpec::listensToTrack(p, t, 8);
    }
    check(alone && sameWithHelpers(p), "core pool: EQ дорожек параллельно — звук тот же");
}
}  // namespace

int runNotesCoreTests() {
    stepNotes();
    piano();
    renderPool();
    std::printf("notes core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
