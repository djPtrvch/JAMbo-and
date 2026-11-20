// MORPH (порт MorphModesTests из ProjectFileTests.swift и testMorphFxAndReturn из TrackFxTests.swift): ручки на все
// дорожки, UNDO не трогает MORPH, возврат, MORPH эффектов дорожки.
#include "model/Project.h"
#include <cmath>
#include <cstdio>
#include <string>

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
bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) <= e; }
double param(const Track& t, const std::string& id) {
    const auto it = t.params.find(id);
    return it == t.params.end() ? 0 : it->second;
}

void shiftAllTracksKeepsOffsets() {
    Project p;
    const ParamDef& f = *Params::byId("filter");
    p.tracks[0].params["filter"] = f.fromNorm(0.5);
    p.tracks[1].params["filter"] = f.fromNorm(0.2);
    p.tracks[2].params["filter"] = f.fromNorm(0.9);
    p.tracks[0].morphParam("filter");
    p.tracks[0].params["filter"] = f.fromNorm(0.6);
    p.morphShift(0, f, 0.6);
    check(near(f.toNorm(param(p.tracks[1], "filter")), 0.3), "morph: сдвиг на ту же долю хода");
    check(near(f.toNorm(param(p.tracks[2], "filter")), 1.0), "morph: у края упирается");
    check(p.tracks[1].morphEdited, "morph: точка на дорожке");
    p.tracks[0].params["filter"] = f.fromNorm(0.5);
    p.morphShift(0, f, 0.5);
    check(near(f.toNorm(param(p.tracks[2], "filter")), 0.9), "morph: вернули ручку — соотношение прежнее");
}

void shiftUsesSelectedTrackDef() {
    Project p;
    p.tracks[0].sample = "mi:16";
    const ParamDef* own = nullptr;
    for (const auto& d : Params::grid("mi:16")) if (d.id == "decay") own = &d;
    const ParamDef* gen = nullptr;
    for (const auto& d : Params::grid(p.tracks[1].sample)) if (d.id == "decay") gen = &d;
    check(own && gen && !near(own->fromNorm(0.3), gen->fromNorm(0.3)), "morph: у KICK свой Decay");
    if (!own || !gen) return;
    p.tracks[0].params["decay"] = own->fromNorm(0.5);
    p.tracks[1].params["decay"] = gen->fromNorm(0.4);
    p.tracks[0].morphParam("decay");
    p.morphShift(0, *own, 0.5);
    check(near(gen->toNorm(param(p.tracks[1], "decay")), 0.4), "morph: та же доля — остальные на месте");
    p.tracks[0].params["decay"] = own->fromNorm(0.7);
    p.morphShift(0, *own, 0.7);
    check(near(gen->toNorm(param(p.tracks[1], "decay")), 0.6), "morph: сдвиг по описанию выбранной дорожки");
}

void keepMorphOnUndo() {
    const ParamDef& f = *Params::byId("filter");
    Project cur;
    cur.tracks[0].params["filter"] = f.fromNorm(0.8);
    cur.tracks[0].morphOrig["filter"] = f.fromNorm(0.2);
    cur.tracks[0].morphEdited = true;
    Project snap = cur;
    snap.tracks[0].morphOrig.clear();
    snap.tracks[0].morphEdited = false;
    snap.tracks[0].params["filter"] = f.fromNorm(0.4);
    snap.tracks[1].params["drive"] = 0.9;
    snap.tracks[1].morphOrig["drive"] = 0.1;
    snap.tracks[1].morphEdited = true;
    snap.keepMorph(cur);
    check(near(f.toNorm(param(snap.tracks[0], "filter")), 0.8), "morph: UNDO — накрученное остаётся");
    check(near(f.toNorm(snap.tracks[0].morphOrig["filter"]), 0.4) && snap.tracks[0].morphEdited,
          "morph: UNDO — исходное из снимка");
    check(param(snap.tracks[1], "drive") == 0.1 && snap.tracks[1].morphOrig.empty() && !snap.tracks[1].morphEdited,
          "morph: MORPH, которого уже нет, — снят");
    snap.tracks[0].undoMorph();
    check(near(f.toNorm(param(snap.tracks[0], "filter")), 0.4), "morph: выключили — к исходному из снимка");
}

void returnGlide() {
    Project p;
    const ParamDef& f = *Params::byId("filter");
    p.tracks[0].params["filter"] = f.fromNorm(0.2);
    p.tracks[0].morphParam("filter");
    p.tracks[0].params["filter"] = f.fromNorm(0.8);
    const auto pts = p.morphReturnPoints();
    check(pts.size() == 1, "morph: точка возврата");
    p.applyMorphReturn(pts, 0.5);
    check(near(f.toNorm(param(p.tracks[0], "filter")), 0.5), "morph: возврат наполовину");
    p.applyMorphReturn(pts, 1);
    check(param(p.tracks[0], "filter") == f.fromNorm(0.2), "morph: возврат до конца");
    check(near(MorphReturn::seconds(3, 120), 2, 1e-9) && MorphReturn::seconds(0, 120) == 0, "morph: RETURN по темпу");
}

void morphFxAndReturn() {
    Project p;
    p.setFx(0, "rev.decay", 1.5);
    p.setFx(1, "rev.decay", 3);
    const ParamDef& def = *Params::byId("rev.decay");
    p.morphFx(0, "rev.decay");
    const double target = 8;
    p.setFx(0, "rev.decay", target);
    const double dn = def.toNorm(target) - def.toNorm(1.5);
    p.morphShiftFx(0, def, def.toNorm(target));
    check(p.tracks[0].morphEdited && p.tracks[1].morphEdited, "morph fx: точка MORPH на дорожках");
    check(near(def.toNorm(p.fxValue(1, "rev.decay")), def.toNorm(3) + dn, 1e-9), "morph fx: сдвиг эффекта");
    const auto pts = p.morphReturnPoints();
    check(pts.size() == p.tracks.size(), "morph fx: все дорожки с клипом");
    p.applyMorphReturn(pts, 0.5);
    check(p.fxValue(0, "rev.decay") < target, "morph fx: возврат");
    for (int i = 0; i < int(p.tracks.size()); ++i)
        if (p.tracks[size_t(i)].morphEdited) p.undoMorph(i);
    check(near(p.fxValue(0, "rev.decay"), 1.5, 1e-9) && near(p.fxValue(1, "rev.decay"), 3, 1e-9),
          "morph fx: исходные значения");
    check(!p.tracks[0].params.count("fx:rev.decay") && !p.tracks[0].morphEdited, "morph fx: ключ не в ручках");
}
}  // namespace

int runMorphTests() {
    shiftAllTracksKeepsOffsets();
    shiftUsesSelectedTrackDef();
    keepMorphOnUndo();
    returnGlide();
    morphFxAndReturn();
    std::printf("morph: %d / %d passed\n", checks - fails, checks);
    return fails;
}
