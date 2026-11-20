// Проекты на диске и текущий проект: порт testStore (ProjectFileTests), ProjectSessionTests, ProjectActionsTests и
// testRandomName (TempoSyncTests) Swift-версии JAMbo.
#include "model/ProjectSession.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace jambo;
namespace fs = std::filesystem;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}

/// Временная папка; удаляется в деструкторе.
struct TempDir {
    std::string path;
    TempDir() {
        std::mt19937_64 g{std::random_device{}()};
        path = (fs::temp_directory_path() / ("jambo-test-" + std::to_string(g()))).string();
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

ProjectInfo info(const std::string& id, const std::string& name, double updated) {
    ProjectInfo i;
    i.id = id;
    i.name = name;
    i.updated = updated;
    return i;
}

void store() {
    TempDir d;
    ProjectStore s(d.path);
    check(s.list().empty(), "store: пусто");
    Project a;
    a.bpm = 97;
    a.tracks[0].setSynth(17);
    check(s.save(a, info("pa", "A", 10)), "store: save pa");
    check(s.save(Project(), info("pb", "B", 20)), "store: save pb");
    ProjectInfo au = info(ProjectStore::autosaveId, "Autosave", 30);
    au.projectId = "pa";
    au.projectName = "A";
    check(s.save(Project(), au), "store: save autosave");
    check(!s.save(Project(), info("../x", "X", 1)), "store: недопустимый id");
    std::ofstream(fs::path(d.path) / "junk.json") << "garbage";
    const auto l = s.list();
    check(l.size() == 3 && l[0].id == ProjectStore::autosaveId && l[1].id == "pb" && l[2].id == "pa",
          "store: список, новые сверху");
    check(!l.empty() && l.back().bpm == 97, "store: bpm в записи списка");
    const auto la = s.load("pa");
    Project ar = a;
    ar.repair();
    check(la && la->doc == ar, "store: load pa");
    check(s.load(ProjectStore::autosaveId)->info.projectId == std::optional<std::string>("pa"), "store: autosave → pa");
    check(s.rename("pa", "A2") && s.load("pa")->info.name == "A2" && s.load("pa")->info.updated == 10,
          "store: rename");
    check(s.find("A2") && s.find("A2")->id == "pa" && !s.find("Autosave"), "store: find");
    s.remove("pb");
    check(!s.load("pb") && s.list().size() == 2, "store: remove");
    check(ProjectStore::newId() != ProjectStore::newId(), "store: newId");
}

void session() {
    {
        TempDir d;
        ProjectStore s(d.path);
        s.save(Project(), info("pa", "Project 1", 1));
        auto [ses, p] = ProjectSession::start(s, std::nullopt);
        check(ses.name == "Project 2" && ses.id != "pa" && ses.unsaved(p) && ses.needsAutosave(p), "session: fresh");
    }
    {
        TempDir d;
        ProjectStore s(d.path);
        Project saved;
        saved.bpm = 100;
        Project work = saved;
        work.bpm = 130;
        s.save(saved, info("pa", "Jam", 1));
        ProjectSession s0("pa", "Jam");
        s.save(work, s0.autosaveInfo(2));
        auto [ses, p] = ProjectSession::start(s, std::string("pa"));
        check(ses.id == "pa" && ses.name == "Jam" && p.bpm == 130, "session: из Autosave");
        check(ses.unsaved(p) && !ses.needsAutosave(p), "session: unsaved, автосохранение не нужно");
        Project back = p;
        back.bpm = 100;
        check(!ses.unsaved(back), "session: как сохранено");
    }
    {
        TempDir d;
        ProjectStore s(d.path);
        Project saved;
        saved.bpm = 90;
        s.save(saved, info("pa", "Jam", 1));
        auto [ses, p] = ProjectSession::start(s, std::string("pa"));
        check(ses.name == "Jam" && p.bpm == 90 && !ses.unsaved(p) && ses.needsAutosave(p), "session: последний проект");
    }
}

void actions() {
    {
        TempDir d;
        ProjectStore s(d.path);
        auto ses = ProjectSession::fresh(s);
        Project p;
        p.bpm = 111;
        check(ses.save(p, s, 5) && !ses.unsaved(p) && s.list().size() == 1 && s.list()[0].name == "Project 1",
              "actions: SAVE");
        const std::string first = ses.id;
        p.bpm = 122;
        check(ses.unsaved(p), "actions: unsaved");
        check(ses.saveAs(p, "  Copy of a very long project name here  ", std::nullopt, s, 6), "actions: SAVE AS");
        check(ses.id != first && ses.name == "Copy of a very long proj" && !ses.unsaved(p), "actions: имя копии");
        check(s.load(first)->doc.bpm == 111 && s.load(ses.id)->doc.bpm == 122, "actions: копия отдельно");
        check(ProjectSession::nextName(s.list()) == "Project 2", "actions: nextName");
    }
    {
        TempDir d;
        ProjectStore s(d.path);
        ProjectSession a("pa", "A");
        a.save(Project(), s, 1);
        ProjectSession b("pb", "B");
        Project p;
        p.bpm = 140;
        check(!b.named("B", s.list()), "actions: своё имя не занято");
        const auto same = b.named(" A ", s.list());
        check(same && same->id == "pa", "actions: имя занято");
        check(b.saveAs(p, "A", same, s, 2) && b.id == "pa" && s.list().size() == 1 && s.load("pa")->doc.bpm == 140,
              "actions: перезапись");
    }
    {
        TempDir d;
        ProjectStore s(d.path);
        ProjectSession ses("pa", "A");
        ses.save(Project(), s, 1);
        s.save(Project(), info("pb", "B", 2));
        ses.rename("A2", s);
        check(ses.name == "A2" && s.load("pa")->info.name == "A2", "actions: RENAME");
        ses.rename("   ", s);
        check(ses.name == "A2", "actions: пустое имя");
        ses.rename("pb", "B2", s);
        check(s.load("pb")->info.name == "B2" && ses.name == "A2", "actions: rename из списка");
        ses.rename("pa", "A3", s);
        check(ses.name == "A3", "actions: rename текущего из списка");
        ses.remove("pa", s);
        check(!s.load("pa") && ses.unsaved(Project()) && ses.name == "A3", "actions: удаление текущего");
    }
    ProjectInfo i = info("p", "x", 0);
    i.bpm = 119.6;
    i.scale = 1;
    check(ProjectSession::meta(i) == "120 · C Min", "actions: meta");
}

void randomName() {
    const std::string a = ProjectSession::randomName({}, 7);
    check(a == ProjectSession::randomName({}, 7) && int(a.size()) <= ProjectSession::nameLimit &&
          a.find(' ') != std::string::npos, "randomName: повторяемо");
    check(ProjectSession::randomName({info("x", a, 0)}, 7) != a, "randomName: занятое не предлагается");
    bool ok = true;
    for (const auto& w : ProjectSession::nameAdjectives)
        for (const auto& n : ProjectSession::nameNouns) ok = ok && int((w + " " + n).size()) <= ProjectSession::nameLimit;
    check(ok, "randomName: длина");
}
}  // namespace

int runStoreTests() {
    store();
    session();
    actions();
    randomName();
    std::printf("store: %d / %d passed\n", checks - fails, checks);
    return fails;
}
