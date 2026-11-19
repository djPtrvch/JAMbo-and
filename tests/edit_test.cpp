// Правки и UNDO: порт EditTests.swift и MorphTests (ветка swift JAMbo) + пианоролл и дорожки проекта.
// Шкалы и подписи параметров из testTailAndParamScales — в tables_test.cpp.
#include "model/History.h"
#include "model/Project.h"
#include "model/Roll.h"
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
std::string place(const std::any& a) {
    const auto* s = std::any_cast<std::string>(&a);
    return s ? *s : "<none>";
}
Track trackWith(int pages) {
    Track t(0);
    for (int i = 1; i < pages; ++i) t.addPage();
    return t;
}
Step& st(Track& t, int p, int s) { return t.pagesMut()[size_t(p)][size_t(s)]; }

void historyUndoRedoAndGesture() {
    History<int> h;
    int v = 1;
    h.record(v, 2); v = 2;
    h.record(v, 2);  // без изменений — нет записи
    h.beginGesture(v);
    h.record(v, 3); v = 3;  // внутри жеста — не пишется
    v = 5;
    h.endGesture(v);  // весь жест — одна запись (2 → 5)
    check(h.undo(v) == 2, "history: undo жеста"); v = 2;
    check(h.undo(v) == 1, "history: undo"); v = 1;
    check(!h.undo(v), "history: нечего отменять");
    check(h.redo(v) == 2, "history: redo"); v = 2;
    h.record(v, 9);
    check(!h.canRedo(), "history: новое действие очищает REDO");
}

void historyPlaces() {
    History<int> h;
    h.record(1, 2, std::string("bass"));
    h.beginGesture(2);
    h.endGesture(3, std::string("hat"));
    check(h.undo(3) == 2 && place(h.lastPlace()) == "hat", "places: undo hat");
    check(h.undo(2) == 1 && place(h.lastPlace()) == "bass", "places: undo bass");
    check(h.redo(1) == 2 && place(h.lastPlace()) == "bass", "places: redo bass");
    check(h.redo(2) == 3 && place(h.lastPlace()) == "hat", "places: redo hat");
    check(h.undo(3) == 2 && place(h.lastPlace()) == "hat", "places: undo hat 2");
}

void historyLimit() {
    History<int> h;
    for (int i = 0; i < 300; ++i) h.record(i, i + 1);
    int v = 300, n = 0;
    while (auto p = h.undo(v)) { v = *p; ++n; }
    check(n == 200 && v == 100, "history: limit 200");
}

void addPageGrows() {
    Track t(0);
    t.addPage();
    check(t.pages().size() == 2 && t.length() == 32 && t.playTo() == 1, "addPage: полная длина растёт");
    t.setLength(20);
    t.setPlayPages(0, 0);
    t.addPage();
    check(t.length() == 36 && t.pages().size() == 3 && t.playTo() == 0, "addPage: 20 → 36");
    for (int i = 0; i < 10; ++i) t.addPage();
    check(int(t.pages().size()) == Params::maxPages, "addPage: до 8");
}

void shortSequence() {
    Track t(0);
    t.setLength(7);
    st(t, 0, 2).on = true;
    st(t, 0, 10).on = true;  // скрытый шаг за длиной
    Track d = t;
    check(d.duplicatePages(0), "short: duplicate");
    check(d.pages().size() == 1 && d.length() == 14, "short: 7 → 14");
    check(d.pages()[0][9].on && !d.pages()[0][10].on, "short: копия шага 3 — шаг 10");
    check(d.playLen(0, 0) == 14, "short: playLen 14");
    Track a = t;
    a.addPage();
    check(a.pages().size() == 2 && a.length() == 23 && a.pageLen(0) == 16 && a.pageLen(1) == 7, "short: + → 23");
    check(!a.pages()[0][10].on && a.playTo() == 1 && a.playLen(0, 1) == 23, "short: новые шаги пустые");
    Track b(0);
    b.setLength(12);
    check(b.duplicatePages(0) && b.length() == 24 && b.pages().size() == 2, "short: 12 → 24");
    Track f(0);
    check(f.duplicatePages(0) && f.length() == 32, "full: duplicate");
}

void duplicatePasteDelete() {
    Track t = trackWith(2);
    st(t, 0, 3).on = true;
    t.setPlayPages(0, 0);
    check(t.duplicatePages(0) && t.pages().size() == 3 && t.pages()[1][3].on, "duplicate: сразу за страницей");
    auto clip = t.copyPages(0);
    t.pastePages(2, clip);
    check(t.pages()[2][3].on, "paste");
    t.setPlayPages(1, 2);
    check(t.deletePages(2), "delete: весь диапазон 1…2");
    check(t.pages().size() == 1 && t.playFrom() == 0, "delete: осталась одна");
    check(!t.deletePages(0), "delete: одна страница остаётся");
}

void deleteSingleInsideRange() {
    Track t = trackWith(4);
    for (int p = 0; p < 4; ++p) { st(t, p, 0).on = true; st(t, p, 0).notes = {60 + p}; }
    t.setPlayPages(0, 3);
    check(t.deletePages(1, true), "single: delete 1");
    bool ok = t.pages().size() == 3;
    const int want[3] = {60, 62, 63};
    for (int p = 0; ok && p < 3; ++p) ok = t.pages()[size_t(p)][0].notes == std::vector<int>{want[p]};
    check(ok && t.playFrom() == 0 && t.playTo() == 2, "single: диапазон сужается");
    t.setPlayPages(1, 2);
    check(t.deletePages(0, true) && t.playFrom() == 0 && t.playTo() == 1, "single: перед диапазоном");
    check(t.deletePages(1, true), "single: delete");
    check(!t.deletePages(0, true) && t.pages().size() == 1, "single: одна остаётся");
}

void nudge() {
    Track t = trackWith(2);
    st(t, 0, 15).on = true;
    st(t, 0, 15).notes = {60};
    t.setPlayPages(0, 1);
    t.nudgePages(0, Track::Nudge::right);
    check(t.pages()[1][0].on, "nudge: через границу страниц");
    t.nudgePages(1, Track::Nudge::left);
    check(t.pages()[0][15].on, "nudge: обратно");
    check(t.nudgePages(0, Track::Nudge::up) && t.pages()[0][15].notes == std::vector<int>{72}, "nudge: октава");
    st(t, 0, 15).notes = {120};
    check(!t.nudgePages(0, Track::Nudge::up) && t.pages()[0][15].notes == std::vector<int>{120}, "nudge: за 127");
}

void transpose() {
    Track t = trackWith(1);
    st(t, 0, 0).on = true;
    st(t, 0, 0).notes = {64, 71};
    check(t.transposePages(0, 1, 0, 0) && t.pages()[0][0].notes == std::vector<int>{65, 72}, "transpose: вверх");
    check(t.transposePages(0, -1, 0, 0) && t.pages()[0][0].notes == std::vector<int>{64, 71}, "transpose: вниз");
    st(t, 0, 0).notes = {61};
    check(t.transposePages(0, 1, 0, 0) && t.pages()[0][0].notes == std::vector<int>{62}, "transpose: C# → D");
    st(t, 0, 0).notes = {127};
    check(!t.transposePages(0, 1, 0, 0) && t.pages()[0][0].notes == std::vector<int>{127}, "transpose: за 127");
}

void locksAndToggle() {
    Track t(0);
    t.toggleStep(2, {36});
    check(t.pages()[0][2].notes == std::vector<int>{36}, "toggle");
    t.setLock(2, "filter", 40);
    t.setLock(5, "pan", 10);
    check(Track::hasManualLocks(t.pages()[0][5]), "p-lock на пустом шаге");
    check(t.value("filter", t.step(2)) == 40, "lock value");
    t.clearLock(2, "filter");
    check(t.value("filter", t.step(2)) == 100, "LP открыт — по умолчанию");
    t.setLock(2, "pan", 5);
    t.clearAllLocks();
    check(t.pages()[0][2].locks.empty() && t.pages()[0][5].locks.empty(), "clearAllLocks");
    t.setLock(2, "pan", 5);
    t.toggleStep(2, {36});
    check(t.pages()[0][2] == Step(), "снятый триг забывает p-lock");
}

void tail() {
    Track t = trackWith(2);
    st(t, 0, 14).on = true;
    st(t, 0, 14).len = 4;
    check(t.isTail(16) && !t.isTail(18), "isTail через границу страниц");
}

void morph() {
    Track t(0);
    t.assigns["pan"] = ModAssign(1, 0.5);
    t.morphParam("filter");
    t.params["filter"] = 40;
    t.morphParam("filter");
    t.params["filter"] = 20;
    t.morphAssign("pan");
    t.assigns["pan"] = ModAssign(2, -1);
    t.morphAssign("level");
    t.assigns["level"] = ModAssign(4, 0.3);
    check(t.morphEdited, "morph: edited");
    t.undoMorph();
    check(t.params["filter"] == 100, "morph: filter вернулся");
    check(t.assigns["pan"] == ModAssign(1, 0.5) && !t.assigns.count("level"), "morph: назначения вернулись");
    check(!t.morphEdited && t.morphOrig.empty(), "morph: сброшен");
}

void roll() {
    Track t(0);
    t.addRollNote(0, 60, 2, std::nullopt);
    t.addRollNote(0, 64, 4, 90.0);
    const Step& s = t.pages()[0][0];
    check(s.on && s.notes == std::vector<int>{60, 64} && s.noteLen(0) == 2 && s.noteLen(1) == 4 && s.len == 4,
          "roll: две ноты с длинами");
    check(!s.noteVel(0) && s.noteVel(1) == 90.0, "roll: velocity");
    t.setRollNoteVel(0, 60, 50);
    check(t.pages()[0][0].noteVel(0) == 50.0 && t.pages()[0][0].locks.at("level") == 90, "roll: velocity ↔ Level");
    t.removeRollNote(0, 64);
    t.removeRollNote(0, 60);
    check(!t.pages()[0][0].on, "roll: без нот — без трига");
    // Перенос с p-lock: нота одна — шаг переезжает целиком.
    t.addRollNote(3, 48, 1, std::nullopt);
    t.setLock(3, "pan", 20);
    auto carry = t.rollCarry(3, 48);
    t.moveRollNote(3, 48, 7, 50, carry);
    check(!t.pages()[0][3].on && t.pages()[0][3].locks.empty(), "roll move: старый шаг пуст");
    check(t.pages()[0][7].notes == std::vector<int>{50} && t.pages()[0][7].locks.at("pan") == 20 && carry && carry->at == 7,
          "roll move: p-lock едет с нотой");
    const auto ns = Roll::notes(t.openClip());
    check(ns.size() == 1 && ns[0].index == 7 && ns[0].pitch == 50, "Roll::notes");
    check(Roll::snap(7, 4) == 4 && Roll::snapLen(2.6, 2, 14, 16) == 2 && Roll::snapLen(0.2, 1, 0, 16) == 1, "Roll::snap");
    check(Roll::rows(0, 0).size() == 75 && Roll::isSharp(61) && Roll::beatLabel(21) == "2.2", "Roll::rows");
}

void projectTracks() {
    Project p;
    p.chanSeqs[Project::mixSeq].pagesMut()[0][0].locks["mix.vol2"] = -5;
    p.chanSeqs[Project::mixSeq].pagesMut()[0][0].locks["mix.vol3"] = -7;
    EqBand b;
    b.dyn = 3;  // источник — дорожка 2 (номер 2 = индекс 2 − 1 + 1)
    p.tracks[0].eq.bands[0] = b;
    check(!p.canDeleteTrack(0), "8 дорожек — удалить нельзя");
    p.addTrack();
    check(p.tracks.size() == 9 && p.tracks[8].stopped && p.tracks[8].clipFilled(0), "addTrack");
    p.deleteTrack(1);
    const auto& locks = p.chanSeqs[Project::mixSeq].pages()[0][0].locks;
    check(!locks.count("mix.vol3") && locks.at("mix.vol2") == -7, "deleteTrack: p-lock MIX сдвинулись");
    check(p.tracks[0].eq.bands[0]->dyn == 2, "deleteTrack: источник DYN сдвинулся");
    p.tracks[0].name = "A";
    p.moveTrack(0, 2);
    check(p.tracks[2].name == "A" && p.tracks[2].eq.bands[0]->dyn == 1, "moveTrack");
    p.renameTrack(1, "  Very long track name  ");
    check(p.tracks[1].name == "Very long tr", "renameTrack: 12 символов");
    Track c = p.tracks[2];
    c.params["filter"] = 33;
    c.name = "B";
    p.pasteTrack(3, c);
    check(p.tracks[3].params["filter"] == 33 && p.tracks[3].name != "B", "pasteTrack: настройки, имя своё");
    check(Project::morphFxId("fx:rev.size") == std::string("rev.size") && !Project::morphFxId("filter"), "morphFxId");
}
}  // namespace

int runEdits() {
    fails = 0;
    historyUndoRedoAndGesture();
    historyPlaces();
    historyLimit();
    addPageGrows();
    shortSequence();
    duplicatePasteDelete();
    deleteSingleInsideRange();
    nudge();
    transpose();
    locksAndToggle();
    tail();
    morph();
    roll();
    projectTracks();
    std::printf("edits: %s\n", fails == 0 ? "all passed" : "FAILED");
    return fails;
}
