// Модель проекта: значения по умолчанию, клип (длина, проход, страницы), открытый клип дорожки, равенство.
#include "model/FilterType.h"
#include "model/KnobDefaults.h"
#include "model/Project.h"
#include "model/SynthEngines.h"
#include <cmath>
#include <cstdio>
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

/// FilterTypeTests.testMigrateProject без файла и repair (они — на шаге 3).
void filterMigration() {
    using namespace FilterMigration;
    const int lp = FilterType::lp, hp = FilterType::hp;
    Project p;
    for (auto& t : p.tracks) t.params["filter"] = 0;
    Track& t0 = p.tracks[0];
    t0.params["filter"] = -40;
    t0.pagesMut()[0][1].locks["filter"] = 50;
    t0.pagesMut()[0][2].locks["filter"] = -20;
    ArrBlock b(0, 0, 16);
    b.autos.push_back({"filter", {{0, 0.3}, {8, 0.8}}});
    t0.blocks.push_back(b);
    p.tracks[1].pagesMut()[0][0].locks["filter"] = 60;
    p.tracks[1].pagesMut()[0][4].locks["filter"] = 30;
    p.tracks[2].pagesMut()[0][3].locks["filter"] = 20;
    p.tracks[2].pagesMut()[0][3].soundLocks = std::vector<std::string>{"filter"};
    p.migrateFilter();
    const Track& a = p.tracks[0];
    check(a.params.at(Params::filterTypeId) == lp && near(a.params.at("filter"), value(-40, lp)), "T1 LP");
    check(a.pages()[0][1].locks.at("filter") == 100, "HP у LP-дорожки — открыт");
    check(near(a.pages()[0][2].locks.at("filter"), value(-20, lp)), "p-lock LP");
    const auto& ys = a.blocks[0].autos[0].points;
    check(near(ys[0].y, value(-40, lp) / 100) && near(ys[1].y, 1), "кривая");
    const Track& c = p.tracks[1];
    check(c.params.at(Params::filterTypeId) == hp && c.params.at("filter") == 0, "центр, p-lock HP — HP");
    check(near(c.pages()[0][0].locks.at("filter"), value(60, hp)), "p-lock HP");
    const Step& s = p.tracks[2].pages()[0][3];
    check(s.locks.at(Params::filterTypeId) == hp && s.soundLocks->size() == 2, "пресет-лок — свой тип");
    check(p.tracks[5].params.at(Params::filterTypeId) == lp && p.tracks[5].params.at("filter") == 100, "центр — LP открыт");
}

/// FilterTypeTests.testPresetAndDefaults: свои значения по умолчанию — прежняя ручка переводится один раз.
void knobDefaults() {
    KnobDefaults k;
    k.values = {{"sampler/filter", 30}, {"synth24/filter", -40}, {"fx/rev.size", 20}};
    k.filterV.reset();
    k.migrateFilter();
    check(k.values.at("sampler/filter.type") == FilterType::hp, "sampler HP");
    check(near(k.values.at("sampler/filter"), FilterMigration::value(30, FilterType::hp)), "sampler value");
    check(k.values.at("synth24/filter.type") == FilterType::lp && k.values.at("fx/rev.size") == 20, "synth24, fx");
    Track t(0);
    t.setSynth(24);
    check(t.params.at("harm") == 100.0 / 6 && t.params.at("timbre") == 40, "SUB: начальные ручки");
    k.applySound(t);
    check(t.params.at(Params::filterTypeId) == FilterType::lp, "тип — вместе со значением");
    t.params["decay"] = 3000;
    t.setSynth(16, &k);
    check(t.params.at("decay") == 1000, "KICK: Decay к пределу");
    Project p;
    k.values["mst/cmp.ratio"] = 4;
    k.values["mod/lfo1.speed"] = 2;
    k.apply(p);
    check(p.fx.at("rev.size") == 20 && p.master.at("cmp.ratio") == 4 && p.tracks[3].params.at("lfo1.speed") == 2,
          "новый проект");
}

}  // namespace

int runModel() {
    fails = 0;
    Project p;
    check(p.tracks.size() == 8 && p.tracks[7].name == "8", "8 дорожек, имена — номера");
    check(p.chanSeqs[0].name == "MIX" && p.chanSeqs[1].name == "MST", "MIX и MST");
    check(p.master.at("cmp.attack") == 30 && p.master.at("lim.release") == 150 && p.master.size() == 22, "мастер");
    check(p.fx.at("rev.hpf") == 120 && p.scenes.size() == 16, "FX и сцены");
    Track& t = p.tracks[0];
    check(t.clips.size() == 16 && t.clips[0] && !t.clips[1], "первый клип");
    check(t.value("decay", nullptr) == 500 && t.value("swing", nullptr) == 50, "значения по умолчанию");
    t.pagesMut()[0][3].locks["decay"] = 100;
    check(t.value("decay", t.step(3)) == 100, "p-lock");
    // Три страницы, последний шаг 40: клип 1–3 — 40 шагов, 1–2 — 32, 3 — 8.
    Clip c;
    c.pages = {emptyPage(), emptyPage(), emptyPage()};
    c.length = 40;
    c.playTo = 2;
    check(c.playLen() == 40, "1–3: 40");
    check(c.playLen(0, 1) == 32 && c.pageLen(2, 2) == 8, "1–2: 32, 3: 8");
    check(c.playIndex(39) == 39 && c.playIndex(40) == 0 && c.outside(41) && !c.outside(39), "playIndex, outside");
    c.playFrom = 2;
    check(c.playLen() == 8 && c.playIndex(1) == 33, "клип 3");
    c.rate = -1;
    check(c.passTicks() == 16 && c.localStep(3) == 1, "скорость 1/2");
    c.rate = 1;
    check(c.passTicks() == 4 && c.localStep(3) == 6, "скорость ×2");
    // Пустая ячейка: открыть — пустой паттерн, правка создаёт клип.
    t.open = 2;
    check(!t.clips[2] && t.pages().size() == 1, "пустая ячейка");
    t.setLength(8);
    check(t.clips[2] && t.clips[2]->length == 8, "правка создаёт клип");
    Project q = p;
    check(q == p, "копия равна");
    q.tracks[1].pagesMut()[0][0].on = true;
    check(!(q == p), "копия независима");
    filterMigration();
    knobDefaults();
    std::printf("model: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
