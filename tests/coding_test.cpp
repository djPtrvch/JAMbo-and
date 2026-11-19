// Файл проекта: тесты ProjectFileTests, FilterTypeTests (перевод проекта и пресета) и SynthKnobsTests (версия 4)
// Swift-версии JAMbo — через JSON и repair.
#include "model/FilterType.h"
#include "model/Json.h"
#include "model/ProjectStore.h"
#include "model/SynthEngines.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace jambo;

namespace {

int fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}
bool near(double a, double b) { return std::fabs(a - b) <= 1e-9; }

Project sample() {
    Project p;
    p.bpm = 97;
    p.scale = 1;
    p.root = 9;
    p.outVol = -std::numeric_limits<double>::infinity();
    p.master["cmp.release"] = MasterChain::autoValue;
    p.tracks[0].sample = "abc123";
    p.tracks[0].folder.items = {FolderItem::ofSample("abc123"), FolderItem::ofSample("mi:3")};
    p.tracks[0].folder.index = 0;
    p.tracks[0].slices["abc123"] = {SliceMark(0.5, 62)};
    p.tracks[0].assigns["filter"] = ModAssign(2, -0.5);
    Step& s = p.tracks[1].pagesMut()[0][3];
    s.on = true;
    s.notes = {60, 64};
    s.lens = std::vector<int>{3, 1};
    s.len = 3;
    s.vels = std::vector<double>{100, -1};
    s.locks["filter"] = 40;
    s.sound = "def456";
    Clip c;
    c.pages = {emptyPage(), emptyPage()};
    c.length = 20;
    c.playTo = 1;
    c.scale = 3;
    c.root = 2;
    c.fx = ParamMap{{"rev.size", 30}};
    p.tracks[2].clips[5] = c;
    EqBand b(true, EqSpec::bell, 300, -4, 2);
    b.dyn = 2;
    p.tracks[3].eq.bands[1] = b;
    p.scenes[2] = SceneRow{"DROP", 4};
    p.rows = 3;
    return p;
}

ProjectInfo info() {
    ProjectInfo i;
    i.id = "p1";
    i.name = "Jam";
    i.updated = 1000;
    return i;
}

void roundTrip() {
    const Project p = sample();
    const auto data = ProjectFormat::encode(ProjectFile::make(info(), p));
    check(data.has_value(), "encode");
    const auto f = ProjectFormat::decode(*data);
    check(f.has_value(), "decode");
    if (!f) return;
    check(f->doc == p, "round trip: проект тот же");
    check(f->info.name == "Jam" && f->info.bpm == 97 && f->info.root == 9 && f->info.scale == 1, "info");
    const Step* s = f->doc.tracks[1].step(3);
    check(s && s->lens == std::vector<int>({3, 1}) && s->vels == std::vector<double>({100, -1}), "lens, vels");
    check(std::isinf(f->doc.outVol) && f->doc.outVol < 0, "−∞ — строкой");
    check(data->find("\"outVol\":\"-inf\"") != std::string::npos, "outVol «-inf»");
    // repair не меняет правильный проект; пустой — 8 дорожек.
    Project r = p;
    r.repair();
    check(r == p, "repair keeps valid project");
    Project q;
    q.repair();
    check(q.tracks.size() == 8 && q.fx == Project().fx, "repair нового");
    const auto keys = p.sampleKeys();
    check(keys == std::set<std::string>({"abc123", "def456"}), "sampleKeys");
}

void missingFields() {
    const std::string js = R"({"format":"jambo-swift","version":1,"future":42,
     "info":{"id":"x","name":"Old","updated":5},
     "doc":{"bpm":999,"scale":77,"rows":0,"newField":{"a":1},
            "tracks":[{"name":"KICK","vol":"loud","params":{"filter":12},
                       "clips":[{"pages":[[{"on":true,"notes":[60,64],"lens":[2],"vels":[90,80]}]],
                                 "length":500,"playTo":9}],
                       "eq":{"bands":[{"type":0,"freq":200,"gain":3,"q":1,"dyn":40}]}}]}})";
    const auto f = ProjectFormat::decode(js);
    check(f.has_value(), "неполный файл читается");
    if (!f) return;
    const Project& d = f->doc;
    check(d.bpm == Params::bpmMax && d.scale == int(Scales::all.size()) - 1 && d.rows == 1, "bpm, scale, rows");
    check(d.tracks.size() == 8 && d.chanSeqs[0].name == "MIX" && d.chanSeqs[1].name == "MST", "дорожки, MIX, MST");
    check(d.scenes.size() == 16 && d.master == MasterChain::defaultValues(), "сцены, мастер");
    const Track& t = d.tracks[0];
    check(t.name == "KICK" && t.vol == Params::volDef, "имя, vol не того типа — по умолчанию");
    check(near(t.params.at("filter"), FilterMigration::value(12, FilterType::hp)), "прежняя ручка Filter — HP");
    check(t.params.at(Params::filterTypeId) == FilterType::hp, "тип HP");
    check(t.params.size() == Params::defaults().size() + 1, "params + тип");
    check(t.clips.size() == 16 && t.clips[0], "клипы");
    const Clip& c = *t.clips[0];
    check(c.pages[0].size() == 16 && c.length == 16 && c.playTo == 0, "страница, длина, playTo");
    const Step& s = c.pages[0][0];
    check(s.on && !s.lens && s.vels == std::vector<double>({90, 80}), "lens не той длины — сняты");
    check(t.eq.bands.size() == 10 && t.eq.bands[0]->dyn == -1 && t.eq.bands[0]->freq == 200, "EQ");
    check(d.tracks[0].clips[0]->fx && d.tracks[0].clips[0]->fx->count("rev.size"), "версия 10: FX в клипах");
}

void codingKeys() {
    // Запись пишет только ключи из списка (testCodingKeysCoverAllFields).
    auto keysOf = [](const Json& o) {
        std::set<std::string> k;
        for (auto it = o.begin(); it != o.end(); ++it) k.insert(it.key());
        return k;
    };
    auto subset = [](const std::set<std::string>& a, const std::set<std::string>& b) {
        for (const auto& x : a) if (!b.count(x)) return false;
        return true;
    };
    check(subset(keysOf(json::put(Project())),
                 {"version", "bpm", "scale", "root", "fx", "fxAssigns", "master", "satBypass", "cmpBypass", "clipBypass",
                  "limBypass", "outVol", "masterEq", "tracks", "chanSeqs", "scenes", "rows", "arrStart", "arrEndStop",
                  "plusCopy", "files", "roots", "rowLoop", "arrowSwitch", "prevRow", "notice", "arrange",
                  "gridHideSteps"}) && keysOf(json::put(Project())).size() == 27, "ключи Project");
    check(keysOf(json::put(Track(0))).size() == 29, "ключи Track (без home и editBlock)");
    check(keysOf(json::put(Clip())) == std::set<std::string>({"pages", "length", "playFrom", "playTo"}), "ключи Clip");
    check(keysOf(json::put(Step())) == std::set<std::string>({"on", "len", "notes", "locks"}), "ключи Step");
    check(keysOf(json::put(EqBand())).size() == 10, "ключи EqBand");
}

void notAProject() {
    check(!ProjectFormat::decode(R"({"format":"other","version":1})"), "другой формат");
    check(!ProjectFormat::decode("not json"), "не JSON");
}

/// FilterTypeTests.testMigrateProject: версия 8 → 10 через файл.
void migrateFilterFile() {
    Project p;
    for (auto& t : p.tracks) t.params["filter"] = 0;
    p.tracks[0].params["filter"] = -40;
    p.tracks[0].pagesMut()[0][1].locks["filter"] = 50;
    p.tracks[0].pagesMut()[0][2].locks["filter"] = -20;
    p.tracks[0].placeBlock(ArrBlock(0, 0, 16));
    p.tracks[0].setAuto("filter", {{0, 0.3}, {8, 0.8}});
    p.tracks[1].params["filter"] = 0;
    p.tracks[1].pagesMut()[0][0].locks["filter"] = 60;
    p.tracks[1].pagesMut()[0][4].locks["filter"] = 30;
    p.tracks[2].pagesMut()[0][3].locks["filter"] = 20;
    p.tracks[2].pagesMut()[0][3].soundLocks = std::vector<std::string>{"filter"};
    p.version = 8;
    Project q;
    check(json::get(Json::parse(json::put(p).dump()), q), "decode");
    q.repair();
    check(q.version == 10, "версия 10");
    const Track& t0 = q.tracks[0];
    check(t0.params.at(Params::filterTypeId) == FilterType::lp, "T1 LP");
    check(t0.pages()[0][1].locks.at("filter") == 100, "HP у LP-дорожки — открыт");
    const auto ys = t0.autoPoints("filter");
    check(ys.size() == 2 && near(ys[0].y, FilterMigration::value(-40, FilterType::lp) / 100) && near(ys[1].y, 1),
          "кривая");
    check(q.tracks[1].params.at(Params::filterTypeId) == FilterType::hp, "T2 HP");
    const Step& s = q.tracks[2].pages()[0][3];
    check(s.locks.at(Params::filterTypeId) == FilterType::hp && s.soundLocks->size() == 2, "пресет-лок");
    Project r = q;
    r.repair();
    check(r == q, "второй repair ничего не меняет");
}

/// testPresetAndDefaults, SynthKnobsTests.testPresetMigration.
void presets() {
    auto p = ProjectCoding::decodeSynthPreset(R"({"name":"Bass","sound":"mi:24","params":{"filter":-50,"harm":10},"fmt":4})");
    check(p && p->fmt == 5 && p->params.at(Params::filterTypeId) == FilterType::lp, "пресет fmt 4 → 5");
    check(p && near(p->params.at("filter"), FilterMigration::value(-50, FilterType::lp)), "пресет Filter");
    auto nf = ProjectCoding::decodeSynthPreset(R"({"name":"A","sound":"mi:24","params":{"harm":10},"fmt":4})");
    check(nf && !nf->params.count("filter") && !nf->value(Params::filterTypeId), "без ручки — без типа");
    auto web = ProjectCoding::decodeSynthPreset(R"({"name":"Pad","sound":"mi:17","params":{"harm":87.5,"timbre":20,"morph":60},"fmt":2})");
    check(web && web->fmt == 5 && web->params.at("timbre") == 87.5 && web->params.at("morph") == 20, "fmt 2: ANALOG");
    check(web && ProjectCoding::decodeSynthPreset(ProjectCoding::encode(*web)) == web, "без повторного перевода");
    const KnobDefaults k = ProjectCoding::decodeKnobDefaults(
        R"({"values":{"sampler/filter":30,"synth24/filter":-40,"fx/rev.size":20}})");
    check(k.filterV == 2 && k.values.at("sampler/filter.type") == FilterType::hp, "KnobDefaults: перевод");
    check(ProjectCoding::decodeKnobDefaults(ProjectCoding::encode(k)) == k, "KnobDefaults: второй раз — то же");
    check(ProjectCoding::decodeKnobDefaults("junk").isEmpty(), "KnobDefaults: мусор — пусто");
}

/// SynthKnobsTests.testMigrateProject: версия 3 → 10.
void migrateSynth() {
    Project p;
    p.version = 3;
    p.tracks[0].name = "Bass";
    p.tracks[0].sample = "mi:17";
    p.tracks[0].params["harm"] = 62.5;
    p.tracks[0].params["timbre"] = 40;
    p.tracks[0].params["morph"] = 25;
    p.tracks[0].assigns["timbre"] = ModAssign(1, 0.3);
    p.tracks[0].assigns["morph"] = ModAssign(2, 0.5);
    p.tracks[0].pagesMut()[0][2].locks = {{"timbre", 80}, {"decay", 100}};
    p.tracks[1].sample = "mi:19";
    p.tracks[1].params["morph"] = 10;
    p.tracks[2].sample = "mi:17";
    p.tracks[2].params["morph"] = 0;
    p.repair();
    const Track& t = p.tracks[0];
    check(p.version == 10 && t.params.at("timbre") == 62.5 && t.params.at("morph") == 40 && t.params.at("p4") == 50,
          "ANALOG: WAVE B, DETUNE");
    check(t.assigns.at("morph") == ModAssign(1, 0.3) && !t.assigns.count("timbre"), "назначения");
    check(t.pages()[0][2].locks == ParamMap({{"morph", 80}, {"decay", 100}}), "p-lock");
    check(near(p.tracks[1].params.at("morph"), 100.0 / 6), "WTABLE UNISON");
    check(p.notice && *p.notice == "SUB removed (ANALOG): Bass", "notice");
    Project q = p;
    q.repair();
    check(q.tracks[0].params == p.tracks[0].params, "повторный repair");
}

}  // namespace

int runCoding() {
    fails = 0;
    roundTrip();
    missingFields();
    codingKeys();
    notAProject();
    migrateFilterFile();
    presets();
    migrateSynth();
    std::printf("coding: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
