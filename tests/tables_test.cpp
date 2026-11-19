// Справочники: Params, Fmt, SynthEngines, FilterType, Scales — проверки из тестов Swift-версии JAMbo
// (EditTests, MasterTests, MasterPresetTests, MasterUnityGuardTests, FilterTypeTests, ChordTests, ScaleRecordTests),
// которые не требуют модели проекта.
#include "model/FilterType.h"
#include "model/KnobDefaults.h"
#include "model/Params.h"
#include "model/Scales.h"
#include "model/SynthEngines.h"
#include "master/MasterChain.h"
#include "synth/Jambo.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
void eq(const std::string& a, const std::string& b, const std::string& what) {
    check(a == b, what + ": «" + a + "» vs «" + b + "»");
}
void near(double a, double b, double acc, const std::string& what) {
    check(std::fabs(a - b) <= acc, what + ": " + std::to_string(a) + " vs " + std::to_string(b));
}
const ParamDef& P(const char* id) {
    static const ParamDef none;
    const ParamDef* p = Params::byId(id);
    check(p != nullptr, std::string("byId ") + id);
    return p ? *p : none;
}

void paramScales() {
    const ParamDef& decay = P("decay");
    near(decay.fromNorm(decay.toNorm(500)), 500, 1e-9, "decay round trip");
    near(decay.fromNorm(0.5), 500, 1e-9, "кубическая шкала: середина — 500 ms");
    const ParamDef& ratchet = P("ratchet");
    check(ratchet.fromNorm(0.4) == 1, "ratchet fromNorm 0.4");
    eq(ratchet.format(3), "4", "ratchet format");
    eq(P("filter").format(100), "LP OFF", "filter 100");
    eq(P("filter").format(0), "LP 20.0", "filter 0");

    // MasterTests
    check(Params::masterGrid().size() == 16, "masterGrid 16");
    for (const auto& p : Params::masterGrid()) check(p.noLock, "noLock " + p.id);
    const ParamDef& rel = P("cmp.release");
    check(rel.fromNorm(1) == MasterChain::autoValue, "release AUTO 1");
    check(rel.fromNorm(0.93) == MasterChain::autoValue, "release AUTO 0.93");
    check(rel.toNorm(MasterChain::autoValue) == 1, "release toNorm AUTO");
    near(rel.fromNorm(0.92 * rel.toNorm(400) / 0.92), 400, 1e-6, "release 400");
    near(rel.fromNorm(0), 20, 1e-9, "release 0");
    const ParamDef& bass = P("clip.bass");
    check(bass.fromNorm(0.04) == 0, "bass OFF");
    check(bass.toNorm(0) == 0, "bass toNorm 0");
    near(bass.fromNorm(0.05), 20, 1e-9, "bass 0.05");
    near(bass.toNorm(300), 1, 1e-9, "bass 300");
    const ParamDef& thr = P("cmp.threshold");
    const double v = thr.fromNorm(0.123);
    near(std::round(v * 10) / 10, v, 1e-9, "threshold step 0.1");
    near(thr.fromNorm(thr.toNorm(-10)), -10, 1e-9, "threshold round trip");
    eq(P("sat.type").format(2), "EDGE", "sat.type");
    eq(Params::masterDb(1.25), "+1.2\ndB", "masterDb");
    eq(Params::masterMs(MasterChain::autoValue), "AUTO", "masterMs");
    // MasterUnityGuardTests, MasterPresetTests
    const ParamDef& g = P("lim.guard");
    eq(g.format(0), "OFF", "guard 0");
    eq(g.format(4), "4.0 dB", "guard 4");
    check(g.fromNorm(0.02) == 0, "guard OFF");
    const ParamDef& ratio = P("cmp.ratio");
    near(ratio.fromNorm(ratio.toNorm(4)), 4, 0.011, "ratio round trip");
    eq(ratio.format(2), "2.0:1", "ratio 2");
    eq(ratio.format(20), "20:1", "ratio 20");

    // Форматы и таблицы.
    eq(Params::volText(-60), "−∞\ndB", "volText min");
    eq(Params::volText(-10), "-10.0\ndB", "volText");
    eq(Fmt::pan(-3.4), "L3", "pan");
    eq(Fmt::pan(0.4), "C", "pan C");
    eq(Fmt::pitch(1.25), "+1.2st", "pitch");
    eq(Fmt::ms(1500), "1.50s", "ms");
    eq(Fmt::ms(12.5), "13ms", "ms half up");
    eq(Fmt::hz(1234), "1.2k", "hz k");
    eq(Params::noteName(48), "C3", "noteName");
    eq(Params::noteName(-1), "B-2", "noteName negative");
    check(Params::mixTrack("mix.vol16") == 15 && Params::mixTrack("mix.vol17") == -1, "mixTrack");
    check(Params::trackGrid().size() == 16 && Params::trackGrid()[8].id == "reverb", "trackGrid rows");
    check(Params::modIds().size() == 16 && Params::modIds()[4] == "lfo2.wave", "modIds");
    check(Params::fxGrid()[4].id == "dly.time" && Params::fxGrid()[4].def == 5, "fx grid");
    eq(P("dly.filter").format(-100), "LP 500", "delay filter");
    eq(P("od.lowcut").format(20), "OFF", "drive low cut");
    eq(P("lfo1.invert").format(0.5), "flat", "lfo invert");
    eq(P("lfo1.polar").format(0.5), "UNI 50%", "lfo polar");
    eq(Params::lfoSpeedSync(1).format(0.05), "8 bars", "lfo sync");
    near(Params::dlyFreeValue(Params::dlyFreeMs(7.3)), 7.3, 1e-9, "dly free");
    check(Params::nearestDelayDiv(500, 120) == 8, "nearest delay 1/4");
    const ParamMap fx = Params::fxDefaults();
    check(fx.size() == 18 && fx.at("rev.hpf") == 120 && fx.at("od.lowcut") == 80, "fxDefaults");
    const ParamMap d = Params::defaults();
    check(d.at("swing") == 50 && d.at("p4") == 50 && d.count("lfo3.speed"), "defaults");
}

void synthGrids() {
    check(SynthEngines::count() == 27, "count");
    check(SynthEngines::engine(15) == nullptr && SynthEngines::engine(26) != nullptr, "engine");
    check(SynthKey::engine("mi:17") == 17 && SynthKey::engine("mi:3") == -1 && SynthKey::engine("s1") == -1, "SynthKey");
    check(Params::synthGrids()[3].empty(), "empty grid");
    const std::string analog = "mi:17";
    const auto& an = Params::grid(analog);
    eq(an[0].label, "Wave A", "ANALOG label");
    check(an[0].kind == KnobKind::stepped && an[0].steps && *an[0].steps == 4, "ANALOG stepped");
    eq(an[0].format(12.5), "Sine", "ANALOG Sine");
    near(an[0].fromNorm(1), 87.5, 1e-9, "середина последнего пункта");
    eq(Params::grid("mi:16")[0].format(50), std::to_string(int(std::round(J::drumPitch(0.5)))) + " Hz", "KICK pitch");
    near(Params::grid("mi:16")[5].max, 1000, 0, "KICK decay max");
    eq(Params::grid("mi:18")[0].format(50), "×2.00", "FM ratio");
    eq(Params::grid("mi:21")[0].format(75), "+6 st", "HAT tune");
    eq(Params::grid("mi:22")[2].format(0), "2.0 ms", "CLAP spread");
    eq(Params::grid("mi:19")[3].format(50), "30 ct", "WTABLE detune");
    eq(Params::grid("mi:25")[0].format(10), "brown", "TEXTURE color");
    eq(Params::grid("mi:26")[2].format(0), "×0.5", "GRANULAR density");
    eq(Params::grid("mi:26")[0].format(25), "25%", "GRANULAR position без сэмпла");
    eq(Params::grid("s1")[0].label, "Pitch", "сэмплер");
    eq(KnobDefaults::soundScope("mi:24"), "synth24", "soundScope");
    eq(KnobDefaults::companion("lfo2.speed"), "lfo2.sync", "companion");
}

void filterTypes() {
    using namespace FilterType;
    const std::string sub = SynthKey::key(J::sub);
    check(next(lp, "s1") == hp && next(hp, "s1") == bp && next(bp, "s1") == lp, "cycle");
    check(next(lp, sub) == ladder && next(ladder, sub) == bp && next(bp, sub) == lp, "cycle SUB");
    check(next(hp, sub) == lp, "HP у SUB — дальше LP");
    check(effective(ladder, "mi:16") == lp && next(ladder, "mi:16") == hp, "LADDER — только у SUB");
    check(of(nullptr) == lp && of(7) == lp, "of");
    check(next(hp, "s1", true) == lp, "back");
    check(valueAfterSwitch(100, lp, hp) == 0 && valueAfterSwitch(0, hp, bp) == 50, "switch open");
    check(valueAfterSwitch(40, lp, hp) == 40 && valueAfterSwitch(100, bp, lp) == 100, "switch");
    for (double v : {-100.0, -60.0, -30.0, -1.0}) {
        const double old = 20000 * std::pow(30.0 / 20000, -v / 100);
        near(Fmt::filterHz(FilterMigration::value(v, lp)), old, old * 1e-9, "old LP");
    }
    for (double v : {1.0, 40.0, 100.0}) {
        const double old = 20 * std::pow(10000.0 / 20, v / 100);
        near(Fmt::filterHz(FilterMigration::value(v, hp)), old, old * 1e-9, "old HP");
    }
    check(FilterMigration::value(0, lp) == 100 && FilterMigration::value(0, hp) == 0, "центр — открыт");
    check(FilterMigration::value(-40, hp) == 0 && FilterMigration::value(40, lp) == 100, "чужой тип — открыт");
    ParamMap pr = {{"filter", -50}, {"harm", 10}};
    FilterMigration::preset(pr);
    check(pr[Params::filterTypeId] == lp, "preset type");
    near(pr["filter"], FilterMigration::value(-50, lp), 1e-9, "preset value");
    ParamMap none = {{"harm", 10}};
    FilterMigration::preset(none);
    check(!none.count("filter") && !none.count(Params::filterTypeId), "без ручки — без типа");
    check(FilterMigration::soundType(nullptr, {30, -20, 60}) == hp, "soundType большинство");
}

void scales() {
    auto name = [](int d, int k) { return Scales::chord(0, 0, 2, d, k); };
    check(name(4, 2) == std::vector<int>({43, 47, 50, 53, 57}), "G9 ноты");
    const auto c = Scales::chord(0, 0, 9, 6, 2);
    check(std::all_of(c.begin(), c.end(), [](int n) { return n <= 127; }) && c.front() == 131 - 12, "верх ниже 127");
    check(Scales::topOctave(0, 0, 2, false) == 7, "C: верх C9 = 120");
    check(Scales::topOctave(0, 11, 2, false) == 6, "B: на октаву ниже");
    int mismatch = 0, out = 0;
    for (int s = 0; s < int(Scales::all.size()); ++s) {
        const auto& steps = Scales::all[size_t(s)].steps;
        for (int root = 0; root < 12; ++root) {
            for (int o = Scales::octaveMin; o <= Scales::topOctave(s, root, 2, true, 2); ++o) {
                const int count = Scales::keyCount(s, 2);
                for (int k = 0; k < count; ++k) {
                    const int n = Scales::note(s, root, o, k);
                    if (std::find(steps.begin(), steps.end(), Scales::wrap(n - root, 12)) == steps.end()) ++mismatch;
                }
                const auto top = Scales::chord(s, root, o, count - 1, 2);
                if (*std::max_element(top.begin(), top.end()) > 127) ++out;
                for (int kind = 0; kind < int(Scales::chordKinds.size()); ++kind)
                    for (int dg = 0; dg < count; ++dg) {
                        const auto ch = Scales::chord(s, root, 2, dg, kind);
                        if (ch.size() != Scales::chordDegrees[size_t(kind)].size() || !std::is_sorted(ch.begin(), ch.end()))
                            ++mismatch;
                    }
            }
        }
    }
    check(mismatch == 0, "клавиши и аккорды — ноты гаммы, вверх от корня");
    check(out == 0, "до topOctave аккорды не выше 127");
}

}  // namespace

int runTables() {
    fails = 0;
    paramScales();
    synthGrids();
    filterTypes();
    scales();
    std::printf("tables: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
