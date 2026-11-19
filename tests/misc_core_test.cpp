// Мелкие тесты через ядро — порт ChordStealTests, DeleteTracksPlayingTests, ToneTests, TempoSyncTests (без
// testRandomName — ProjectSession не перенесён), KickDriveTests (ветка swift JAMbo).
#include "engine/AudioCore.h"
#include "fx/FxRack.h"
#include "model/FilterType.h"
#include "model/Params.h"
#include "model/Project.h"
#include "model/Scales.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

std::vector<float> render(AudioCore& core, int frames) {
    std::vector<float> out(static_cast<size_t>(frames)), r(256);
    for (int pos = 0; pos < frames;) {
        const int n = std::min(256, frames - pos);
        core.render(n, out.data() + pos, r.data());
        while (core.sequencer.out.pop()) {}
        pos += n;
    }
    return out;
}

/// Наибольшая вторая разность выхода (щелчок) за frames кадров; skip(f) — пропустить отсчёт f.
template <typename F>
float maxJump(AudioCore& core, int frames, F skip) {
    std::vector<float> bl(256), br(256);
    float a = 0, b = 0, m = 0;
    int f = 0;
    for (int k = 0; k < frames / 256; ++k) {
        core.render(256, bl.data(), br.data());
        for (float x : bl) {
            if (!skip(f)) m = std::max(m, std::fabs(x - 2 * a + b));
            b = a;
            a = x;
            ++f;
        }
    }
    return m;
}

/// Аккорды больше голосов дорожки (6): смена аккорда крадёт голоса — без щелчка.
float chordJump(int kind) {
    Project p;
    p.tracks[0].setSynth(17);
    p.tracks[0].params["attack"] = 203;
    p.tracks[0].params["decay"] = 2030;
    p.tracks[0].params["reverb"] = 0;
    p.tracks[0].params["filter"] = FilterMigration::value(-30, FilterType::lp);
    std::vector<Page> pages;
    for (int d : {1, 7, 6, 7}) {
        Page page = emptyPage();
        page[0].on = true;
        page[0].len = 16;
        page[0].notes = Scales::chord(3, 4, 2, d - 1, kind);
        pages.push_back(page);
    }
    p.tracks[0].pagesMut() = pages;
    p.tracks[0].setPlayTo(3);
    AudioCore core;
    core.installSynth();
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    return maxJump(core, 7 * 48000, [](int) { return false; });
}

void chordSteal() {
    check(chordJump(1) < 0.003f, "chord steal: 7th — без щелчка");
    check(chordJump(2) < 0.003f, "chord steal: 9th — без щелчка");
}

void deleteWhilePlaying() {
    bool ok = true;
    for (bool fromStart : {false, true}) {
        Project p;
        while (int(p.tracks.size()) < 16) p.addTrack();
        for (auto& t : p.tracks) {
            if (!t.clips[0]) continue;
            for (int k = 0; k < 16; k += 2) t.clips[0]->pages[0][size_t(k)].on = true;
        }
        AudioCore core;
        core.publish(p);
        core.events.push(CoreEvent::play(true));
        render(core, 40 * 256);
        while (p.canDeleteTrack(fromStart ? 0 : int(p.tracks.size()) - 1)) {
            p.deleteTrack(fromStart ? 0 : int(p.tracks.size()) - 1);
            core.publish(p);
            render(core, 20 * 256);
        }
        ok = ok && int(p.tracks.size()) == Params::trackCount;
    }
    check(ok, "delete tracks: удаление во время игры");
}

void tone() {
    {
        AudioCore core;
        const auto x = render(core, 4800);
        float m = 0;
        for (float v : x) m = std::max(m, std::fabs(v));
        check(m == 0 && core.framesRendered() == 4800, "tone: тишина, пока не включён");
    }
    AudioCore core;
    core.events.push(CoreEvent::tone(true));
    const auto x = render(core, 48000);
    double s = 0;
    for (int i = 24000; i < 48000; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    const double db = 20 * std::log10(std::sqrt(s / 24000) * std::sqrt(2.0));
    int crossings = 0;
    for (int i = 24001; i < 48000; ++i)
        if (x[size_t(i - 1)] < 0 && x[size_t(i)] >= 0) ++crossings;
    float step = 0;
    for (size_t i = 1; i < x.size(); ++i) step = std::max(step, std::fabs(x[i] - x[i - 1]));
    check(std::fabs(db + 12) < 0.01 && std::abs(crossings - 220) <= 1 && step < 0.015f,
          "tone: 440 Гц −12 dBFS, плавное включение");
    core.events.push(CoreEvent::tone(false));
    const auto y = render(core, 9600);
    float stepOff = 0, tail = 0;
    for (size_t i = 1; i < y.size(); ++i) stepOff = std::max(stepOff, std::fabs(y[i] - y[i - 1]));
    for (size_t i = y.size() - 1000; i < y.size(); ++i) tail = std::max(tail, std::fabs(y[i]));
    check(stepOff < 0.015f && tail == 0, "tone: плавное выключение, через 200 мс тишина");
    CPUMeter m(48000, 0.5);
    for (int i = 0; i < 100; ++i) m.measure(1'066'666, 256);
    check(std::fabs(m.load() - 0.2f) < 0.001f && std::fabs(m.peak() - 0.2f) < 0.001f, "cpu: средняя и пик");
}

void tempoSync() {
    const auto& g = Params::fxGrid();
    std::set<std::string> a(Params::fxIds.begin(), Params::fxIds.end()), b;
    for (const auto& p : g) b.insert(p.id);
    for (const auto& p : Params::hiddenFx()) b.insert(p.id);
    check(g[6].id == "dly.spread" && g[7].id == "dly.filter" && a == b && int(Params::fxIds.size()) == Params::fxCount,
          "fx: порядок и состав");
    check(Params::fxIds[FxRack::dlyFilter] == "dly.filter" && Params::fxIds[FxRack::dlySpread] == "dly.spread" &&
              Params::fxIds[FxRack::dlySync] == "dly.sync" && Params::fxIds[FxRack::scCurve] == "sc.curve",
          "fx: номера FxRack");
    check(Params::byId("sidechain") && Params::byId("sidechain")->label == "Duck" && Params::byId("sc.attack") &&
              Params::byId("sc.attack")->label == "Duck Attack",
          "fx: подписи Duck");
    int quarterDly = -1;
    for (size_t i = 0; i < Params::delayDivs.size(); ++i)
        if (std::string(Params::delayDivs[i].name) == "1/4") quarterDly = int(i);
    check(std::fabs(Params::dlyFreeMs(0) - 10) < 1e-9 && std::fabs(Params::dlyFreeMs(11) - 2000) < 1e-6 &&
              std::fabs(Params::dlyFreeMs(Params::dlyFreeValue(375)) - 375) < 1e-6 &&
              Params::nearestDelayDiv(500, 120) == quarterDly &&
              Params::dlyTimeFree().format(Params::dlyFreeValue(250)) == "250ms",
          "delay: свободное время");
    const auto& divs = Params::lfoDivs;
    check(Params::lfoDivIndex(Params::lfoSpeedMin) == 0 && Params::lfoDivIndex(Params::lfoSpeedMax) == int(divs.size()) - 1 &&
              Params::lfoSpeedSync(1).format(Params::lfoSpeedMin) == "8 bars",
          "lfo: пункты по темпу");
    int quarter = 0;
    for (size_t i = 0; i < divs.size(); ++i)
        if (std::string(divs[i].name) == "1/4") quarter = int(i);
    const double x = double(quarter) / double(divs.size() - 1);
    const double speed = Params::lfoSpeedMin * std::pow(Params::lfoSpeedMax / Params::lfoSpeedMin, x);
    AudioCore core;
    Project p;
    p.bpm = 120;
    p.tracks[0].params["lfo1.speed"] = speed;
    p.tracks[0].params["lfo1.sync"] = 1;
    core.publish(p);
    std::vector<float> bl(240), br(240);
    core.render(240, bl.data(), br.data());
    const double ph0 = core.mod.phaseOuts[0].load();
    for (int i = 0; i < 100; ++i) core.render(240, bl.data(), br.data());
    const double ph1 = core.mod.phaseOuts[0].load();
    const double d = std::fmod(ph1 - ph0 + 1, 1.0);
    check(std::min(d, 1 - d) < 1e-4, "lfo: 1/4 при 120 BPM — целый оборот за 0,5 с");
}

void kickDrive() {
    Project p;
    p.tracks[0].setSynth(16);
    Page page = emptyPage();
    for (int s = 0; s < 16; s += 4) {
        page[size_t(s)].on = true;
        page[size_t(s)].notes = {36};
    }
    p.tracks[0].pagesMut() = {page};
    AudioCore core;
    core.installSynth();
    core.publish(p);
    core.events.push(CoreEvent::play(true));
    std::vector<float> bl(256), br(256);
    float a = 0, b = 0, m = 0;
    int f = 0;
    for (int k = 0; k < 2 * 48000 / 256; ++k) {
        if (k % 2 == 0) {
            // DRIVE (ручка 3 движка — morph) быстро туда-обратно, как пальцем.
            p.tracks[0].params["morph"] = 50 + 50 * std::sin(double(k) * 0.4);
            core.publish(p);
        }
        core.render(256, bl.data(), br.data());
        for (float v : bl) {
            if (f % 24000 > 1500) m = std::max(m, std::fabs(v - 2 * a + b));
            b = a;
            a = v;
            ++f;
        }
    }
    check(m < 0.0015f, "kick: вращение DRIVE — без треска");
}
}  // namespace

int runMiscCoreTests() {
    chordSteal();
    deleteWhilePlaying();
    tone();
    tempoSync();
    kickDrive();
    std::printf("misc core: %d / %d passed\n", checks - fails, checks);
    return fails;
}
