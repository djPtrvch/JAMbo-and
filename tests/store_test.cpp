// Проекты на диске и текущий проект: порт testStore (ProjectFileTests), ProjectSessionTests, ProjectActionsTests и
// testRandomName (TempoSyncTests) Swift-версии JAMbo.
#include "model/ProjectSession.h"
#include "model/ProjectPack.h"
#include "files/SampleStore.h"
#include "synth/Wavetable.h"
#include <cmath>
#include <cstdio>
#include <limits>
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

void projectPack() {
    TempDir temp;
    const fs::path root(temp.path);
    const ProjectPack::Storage source{root / "export-samples", root / "export-tables"};
    const ProjectPack::Storage dest{root / "import-samples", root / "import-tables"};
    fs::create_directories(source.samples);
    fs::create_directories(source.tables);
    const std::vector<float> left{0.25f, -0.5f, 0.75f, 1.f}, right{0.5f, 0.f, -0.25f, 0.f};
    const SampleData sample(left, &right);
    for (const auto& key : {"kick", "step", "reference"})
        check(SampleStore::write(sample, SampleStore::path(key, source.samples.string())), "pack: sample fixture");
    std::vector<float> wave(512);
    for (size_t i = 0; i < wave.size(); ++i) wave[i] = std::sin(6.28318530718 * (i % 256) / 256);
    const auto wav = AudioFile::wavFloat32(wave, nullptr);
    std::ofstream(source.tables / "custom.wav", std::ios::binary)
        .write(reinterpret_cast<const char*>(wav.data()), std::streamsize(wav.size()));

    Project project;
    project.bpm = 101;
    project.outVol = -std::numeric_limits<double>::infinity();
    project.tracks[0].sample = "kick";
    project.tracks[0].table = WavetableKey::user("custom", 256);
    project.tracks[0].withStep(0, [](Step& st) { st.sound = "step"; });
    project.arrange.ref = ArrRef{"reference", "Reference", 0, 0, std::nullopt};
    project.files["unused"] = "Unused file without any reference";
    const auto path = root / "export.jambo";
    check(ProjectPack::write(path, "Jam", project, source) == 0, "pack: export includes project media only");
    {
        std::ifstream file(path, std::ios::binary);
        unsigned char prefix[8]{};
        file.read(reinterpret_cast<char*>(prefix), sizeof(prefix));
        const uint32_t len = uint32_t(prefix[4]) << 24 | uint32_t(prefix[5]) << 16 |
                             uint32_t(prefix[6]) << 8 | uint32_t(prefix[7]);
        check(std::string(reinterpret_cast<char*>(prefix), 4) == "JMB1" && file && len > 0 && len < fs::file_size(path) - 8, "pack: Swift magic and big-endian JSON length");
    }
    const auto back = ProjectPack::read(path, dest);
    check(back && back->missing == 0 && back->name == "Jam" && back->project.bpm == 101 &&
              back->project.outVol == project.outVol && back->project.tracks[0].table == project.tracks[0].table,
          "pack: project and metadata round trip");
    for (const auto& key : {"kick", "step", "reference"}) {
        const auto data = SampleStore::read(key, dest.samples.string());
        check(data && data->frames() == 4 && data->channels() == 2 && data->right()[2] == -0.25f,
              "pack: exact sample data under its original key");
    }
    const auto user = WtTable::readUserWave(dest.tables / "custom.wav");
    const auto table = user ? WtTable::user(*user, 256) : nullptr;
    check(user && *user == wave && table && table->frames == 2 && std::abs(table->ref().data[0][512] - 1) < 1e-5,
          "pack: user wavetable transfers as WAV and renders");

    const SampleData existing(std::vector<float>{-0.75f}, nullptr);
    SampleStore::write(existing, SampleStore::path("kick", dest.samples.string()));
    const auto repeat = ProjectPack::read(path, dest);
    const auto preserved = SampleStore::read("kick", dest.samples.string());
    check(repeat && preserved && preserved->frames() == 1 && preserved->left()[0] == -0.75f,
          "pack: reimport keeps existing media");
    const auto broken = root / "broken.jambo";
    fs::copy_file(path, broken);
    fs::resize_file(broken, fs::file_size(broken) - 2);
    const ProjectPack::Storage empty{root / "untouched", root / "unused-tables"};
    check(!ProjectPack::read(broken, empty) && !fs::exists(empty.samples), "pack: truncated archive changes no media");

    // Заголовок собран отдельно, в порядке полей JSONSerialization Swift.
    const std::string header = R"({"samples":[],"doc":{"bpm":103,"version":10,"outVol":"-inf"},"name":"Swift","version":1,"format":"jambo-swift"})";
    const auto fixture = root / "swift.jambo";
    {
        std::ofstream file(fixture, std::ios::binary);
        const char prefix[8] = {'J', 'M', 'B', '1', 0, 0, char(header.size() >> 8), char(header.size())};
        file.write(prefix, sizeof(prefix)) << header;
    }
    const auto swift = ProjectPack::read(fixture, empty);
    check(swift && swift->missing == 0 && swift->name == "Swift" && swift->project.bpm == 103,
          "pack: imports Swift header independently of export");
    const auto missing = root / "missing.jambo";
    fs::remove(source.samples / "step.caf");
    check(ProjectPack::write(missing, "Missing", project, source) == 1, "pack: export reports unavailable sample");
    const auto partial = ProjectPack::read(missing, {root / "partial", root / "partial-waves"});
    check(partial && partial->missing == 1, "pack: missing sample is reported on import");

    const std::vector<ProjectInfo> names{info("pa", "Jam", 0), info("pb", "Jam (imported)", 0)};
    check(ProjectSession::importedName(" Jam ", names) == "Jam (imported 2)", "pack: names remain unique after repeated import");
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
    projectPack();
    session();
    actions();
    randomName();
    std::printf("store: %d / %d passed\n", checks - fails, checks);
    return fails;
}
