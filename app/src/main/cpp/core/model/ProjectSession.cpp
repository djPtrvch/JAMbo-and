// Порт Core/Model/ProjectSession.swift (ветка swift JAMbo).
#include "ProjectSession.h"
#include "Scales.h"
#include <cmath>
#include <random>
#include <set>

namespace jambo {

const char* const ProjectSession::autosaveName = "Autosave";

namespace {
const char* const noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct SplitMix {
    uint64_t state;
    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

std::set<std::string> names(const std::vector<ProjectInfo>& list) {
    std::set<std::string> out;
    for (const auto& i : list) out.insert(i.name);
    return out;
}

bool space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && space((unsigned char)s[a])) ++a;
    while (b > a && space((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

/// Первые n символов UTF-8 (по кодовым точкам).
std::string prefix(const std::string& s, int n) {
    size_t i = 0;
    int k = 0;
    while (i < s.size() && k < n) {
        const unsigned char c = (unsigned char)s[i];
        i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        ++k;
    }
    return s.substr(0, std::min(i, s.size()));
}
}  // namespace

const std::vector<std::string> ProjectSession::nameAdjectives = {
    "Velvet", "Neon", "Midnight", "Golden", "Electric", "Silent", "Cosmic", "Liquid", "Crystal", "Lazy", "Wild",
    "Hidden", "Broken", "Lunar", "Solar", "Dusty", "Frozen", "Burning", "Purple", "Deep", "Hazy", "Rusty",
    "Silver", "Analog", "Distant", "Funky", "Mellow", "Stormy", "Sleepy", "Magnetic",
};
const std::vector<std::string> ProjectSession::nameNouns = {
    "Groove", "Echo", "Pulse", "Orbit", "Tide", "Comet", "Mirage", "Signal", "Drift", "Loop", "Garden",
    "Engine", "Shadow", "Wave", "Circuit", "Horizon", "Ember", "Canyon", "Harbor", "Prism", "Rhythm", "Static",
    "Tape", "Voltage", "Bloom", "Desert", "River", "Ritual", "Machine", "Sunset",
};

ProjectInfo ProjectSession::autosaveInfo(double now) const {
    ProjectInfo i;
    i.id = ProjectStore::autosaveId;
    i.name = autosaveName;
    i.updated = now;
    i.projectId = id;
    i.projectName = name;
    return i;
}

ProjectInfo ProjectSession::info(double now) const {
    ProjectInfo i;
    i.id = id;
    i.name = name;
    i.updated = now;
    return i;
}

std::string ProjectSession::nextName(const std::vector<ProjectInfo>& list) {
    const auto taken = names(list);
    int n = 1;
    while (taken.count("Project " + std::to_string(n))) ++n;
    return "Project " + std::to_string(n);
}

std::string ProjectSession::randomName(const std::vector<ProjectInfo>& list, std::optional<uint64_t> seed) {
    SplitMix g{seed ? *seed : std::random_device{}() * 0x100000001ull + std::random_device{}()};
    const auto taken = names(list);
    std::string last;
    for (int i = 0; i < 20; ++i) {
        const std::string& a = nameAdjectives[size_t(g.next() % nameAdjectives.size())];
        const std::string& n = nameNouns[size_t(g.next() % nameNouns.size())];
        last = a + " " + n;
        if (!taken.count(last)) return last;
    }
    int k = 2;
    while (taken.count(last + " " + std::to_string(k))) ++k;
    return last + " " + std::to_string(k);
}

ProjectSession ProjectSession::fresh(const ProjectStore& store) {
    return ProjectSession(ProjectStore::newId(), nextName(store.list()));
}

ProjectSession ProjectSession::open(const ProjectFile& f, const ProjectStore& store) {
    if (f.info.id != ProjectStore::autosaveId) return ProjectSession(f.info.id, f.info.name, f.doc);
    const std::string i = f.info.projectId.value_or(ProjectStore::newId());
    const std::string n = f.info.projectName ? *f.info.projectName : nextName(store.list());
    std::optional<Project> saved;
    if (auto s = store.load(i)) saved = s->doc;
    return ProjectSession(i, n, saved, f.doc);
}

std::pair<ProjectSession, Project> ProjectSession::start(const ProjectStore& store,
                                                        const std::optional<std::string>& last) {
    if (auto f = store.load(ProjectStore::autosaveId)) return {open(*f, store), f->doc};
    if (last)
        if (auto f = store.load(*last)) return {open(*f, store), f->doc};
    Project p;
    p.repair();
    return {fresh(store), p};
}

std::string ProjectSession::clean(const std::string& name) { return prefix(trim(name), nameLimit); }

bool ProjectSession::save(const Project& p, const ProjectStore& store, double now) {
    if (!store.save(p, info(now))) return false;
    saved = p;
    return true;
}

bool ProjectSession::saveAs(const Project& p, const std::string& nm, const std::optional<ProjectInfo>& replace,
                            const ProjectStore& store, double now) {
    const std::string n = clean(nm);
    id = replace ? replace->id : ProjectStore::newId();
    name = n.empty() ? nextName(store.list()) : n;
    saved.reset();
    return save(p, store, now);
}

void ProjectSession::rename(const std::string& nm, const ProjectStore& store) {
    const std::string n = clean(nm);
    if (n.empty()) return;
    name = n;
    store.rename(id, n);
}

void ProjectSession::rename(const std::string& other, const std::string& nm, const ProjectStore& store) {
    if (other == id) {
        rename(nm, store);
        return;
    }
    const std::string n = clean(nm);
    if (!n.empty()) store.rename(other, n);
}

void ProjectSession::remove(const std::string& other, const ProjectStore& store) {
    store.remove(other);
    if (other == id) saved.reset();
}

std::optional<ProjectInfo> ProjectSession::named(const std::string& nm, const std::vector<ProjectInfo>& list) const {
    const std::string n = clean(nm);
    for (const auto& i : list)
        if (i.name == n && i.id != id && i.id != ProjectStore::autosaveId) return i;
    return std::nullopt;
}

std::string ProjectSession::meta(const ProjectInfo& i) {
    return std::to_string(int(std::round(i.bpm))) + " · " + keyName(i);
}

std::string ProjectSession::keyName(const ProjectInfo& i) {
    const int scale = std::min(int(Scales::all.size()) - 1, std::max(0, i.scale));
    return std::string(noteNames[((i.root % 12) + 12) % 12]) + " " + Scales::all[size_t(scale)].shortName;
}

std::string ProjectSession::importedName(const std::string& nm, const std::vector<ProjectInfo>& list) {
    const std::string n = trim(nm);
    const std::string base = n.empty() ? nextName(list) : n;
    for (const auto& i : list)
        if (i.name == base) return base + " (imported)";
    return base;
}

std::string ProjectSession::exportFileName(const std::string& nm) {
    std::string out;
    for (unsigned char c : nm) {
        // Буквы (в том числе не ASCII — байты UTF-8 не трогаются), цифры, «-», «_», пробел; прочее — «_».
        const bool ok = c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '-' || c == '_' || c == ' ';
        out.push_back(ok ? char(c) : '_');
    }
    return (out.empty() ? std::string("project") : out) + ".jambo";
}

}  // namespace jambo
