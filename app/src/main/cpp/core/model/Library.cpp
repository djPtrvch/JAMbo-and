// Порт Core/Model/Library.swift (ветка swift JAMbo).
#include "Library.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include "ProjectStore.h"

namespace jambo {

namespace fs = std::filesystem;

const char* const Library::root = "root";
const char* const Library::recordings = "recordings";

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

static std::string radix36(unsigned long long v) {
    static const char* d = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (v == 0) return "0";
    std::string s;
    while (v > 0) {
        s.insert(s.begin(), d[v % 36]);
        v /= 36;
    }
    return s;
}

void Library::ensureRecordings() {
    for (const auto& n : nodes)
        if (n.id == recordings) return;
    LibNode n;
    n.id = recordings;
    n.parent = root;
    n.kind = LibKind::folder;
    n.name = "RECORDINGS";
    n.system = true;
    n.updated = 0;
    nodes.insert(nodes.begin(), n);
}

std::string Library::newId() {
    static std::mt19937_64 rng{std::random_device{}()};
    const auto t = (unsigned long long)(nowSec() * 1000);
    const auto r = std::uniform_int_distribution<unsigned long long>(0, 36ull * 36 * 36 * 36 * 36 - 1)(rng);
    return "n" + radix36(t) + radix36(r);
}

const LibNode* Library::node(const std::string& id) const {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

bool Library::isFolder(const std::string& id) const {
    if (id == root) return true;
    const LibNode* n = node(id);
    return n && n->kind == LibKind::folder;
}

static int rank(const LibNode& n) {
    if (n.isSystem()) return 0;
    return n.kind == LibKind::folder ? 1 : 2;
}

bool Library::nameLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && a[i2] == '0') i2++;
            while (j2 < b.size() && b[j2] == '0') j2++;
            size_t ie = i2, je = j2;
            while (ie < a.size() && std::isdigit((unsigned char)a[ie])) ie++;
            while (je < b.size() && std::isdigit((unsigned char)b[je])) je++;
            if (ie - i2 != je - j2) return ie - i2 < je - j2;
            const int c = a.compare(i2, ie - i2, b, j2, je - j2);
            if (c != 0) return c < 0;
            i = ie;
            j = je;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        i++;
        j++;
    }
    return (a.size() - i) < (b.size() - j);
}

std::vector<LibNode> Library::children(const std::string& parent) const {
    std::vector<LibNode> list;
    for (const auto& n : nodes)
        if (n.parent == parent) list.push_back(n);
    std::stable_sort(list.begin(), list.end(), [](const LibNode& a, const LibNode& b) {
        const int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        if (ra == 1 && a.order != b.order) {
            if (!a.order) return false;
            if (!b.order) return true;
            return *a.order < *b.order;
        }
        return nameLess(a.name, b.name);
    });
    return list;
}

std::vector<LibNode> Library::movableFolders(const std::string& parent) const {
    std::vector<LibNode> out;
    for (auto& n : children(parent))
        if (n.kind == LibKind::folder && !n.isSystem()) out.push_back(n);
    return out;
}

void Library::moveFolder(const std::string& parent, int from, int to) {
    std::vector<std::string> ids;
    for (auto& n : movableFolders(parent)) ids.push_back(n.id);
    const int c = int(ids.size());
    if (from < 0 || from >= c || to < 0 || to >= c || from == to) return;
    const std::string id = ids[size_t(from)];
    ids.erase(ids.begin() + from);
    ids.insert(ids.begin() + to, id);
    for (size_t k = 0; k < ids.size(); k++)
        for (auto& n : nodes)
            if (n.id == ids[k]) n.order = double(k);
}

std::vector<LibNode> Library::path(const std::string& id) const {
    std::vector<LibNode> out;
    const LibNode* cur = node(id);
    int guard = 0;
    while (cur && guard < 64) {
        out.insert(out.begin(), *cur);
        cur = node(cur->parent);
        guard++;
    }
    return out;
}

/// Префикс не длиннее n символов UTF-8.
static std::string prefixChars(const std::string& s, int n) {
    size_t i = 0;
    int k = 0;
    while (i < s.size() && k < n) {
        const unsigned char c = (unsigned char)s[i];
        i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        k++;
    }
    return s.substr(0, std::min(i, s.size()));
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string Library::clean(const std::string& name) {
    const std::string t = trim(prefixChars(trim(name), nameLimit));
    return t.empty() ? "Untitled" : t;
}

static const std::set<std::string> audioExts = {"wav", "wave", "aif", "aiff", "caf", "mp3", "m4a", "aac", "flac"};

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::string Library::bare(const std::string& name) {
    const auto dot = name.rfind('.');
    std::string base = name;
    if (dot != std::string::npos && dot > 0 && audioExts.count(lower(name.substr(dot + 1)))) base = name.substr(0, dot);
    return lower(base);
}

std::string Library::uniqueName(const std::string& parent, const std::string& name, const std::string* except) const {
    const std::string base = clean(name);
    std::set<std::string> taken;
    for (const auto& n : nodes)
        if (n.parent == parent && (!except || n.id != *except)) taken.insert(bare(n.name));
    if (!taken.count(bare(base))) return base;
    int k = 2;
    while (taken.count(bare(base + " " + std::to_string(k)))) k++;
    return base + " " + std::to_string(k);
}

const LibNode* Library::sameName(const std::string& parent, const std::string& name) const {
    const std::string b = bare(clean(name));
    for (const auto& n : nodes)
        if (n.parent == parent && n.kind != LibKind::folder && bare(n.name) == b) return &n;
    return nullptr;
}

int Library::countInside(const std::string& id) const {
    int c = 0;
    for (const auto& x : nodes)
        if (x.parent == id) {
            c++;
            if (x.kind == LibKind::folder) c += countInside(x.id);
        }
    return c;
}

static LibNode make(const std::string& parent, LibKind kind, const std::string& name) {
    LibNode n;
    n.id = Library::newId();
    n.parent = parent;
    n.kind = kind;
    n.name = name;
    n.updated = nowSec();
    return n;
}

const LibNode& Library::createFolder(const std::string& parent, const std::string& name) {
    nodes.push_back(make(parent, LibKind::folder, uniqueName(parent, name)));
    return nodes.back();
}

const LibNode& Library::addSample(const std::string& parent, const std::string& key, const std::string& name) {
    for (const auto& n : nodes)
        if (n.parent == parent && n.kind == LibKind::sample && n.key == key) return n;
    LibNode n = make(parent, LibKind::sample, uniqueName(parent, name));
    n.key = key;
    nodes.push_back(n);
    return nodes.back();
}

const LibNode& Library::addTable(const std::string& parent, const std::string& key, const std::string& name) {
    for (const auto& n : nodes)
        if (n.parent == parent && n.kind == LibKind::table && n.key == key) return n;
    LibNode n = make(parent, LibKind::table, uniqueName(parent, name));
    n.key = key;
    nodes.push_back(n);
    return nodes.back();
}

const LibNode& Library::addPreset(const std::string& parent, const SynthPreset& preset) {
    const std::string name = uniqueName(parent, preset.name);
    LibNode n = make(parent, LibKind::preset, name);
    n.preset = preset;
    n.preset->name = name;
    nodes.push_back(n);
    return nodes.back();
}

void Library::overwrite(const std::string& id, const FolderItem& item) {
    for (auto& n : nodes) {
        if (n.id != id || n.kind == LibKind::folder) continue;
        if (item.preset) {
            SynthPreset p = *item.preset;
            p.name = n.name;
            n.kind = LibKind::preset;
            n.preset = p;
            n.key.reset();
        } else {
            n.kind = LibKind::sample;
            n.key = item.sample;
            n.preset.reset();
        }
        n.updated = nowSec();
        return;
    }
}

void Library::rename(const std::string& id, const std::string& name) {
    if (trim(name).empty()) return;
    for (auto& n : nodes) {
        if (n.id != id || n.isSystem()) continue;
        const std::string nn = uniqueName(n.parent, name, &id);
        n.name = nn;
        if (n.preset) n.preset->name = nn;
        n.updated = nowSec();
        return;
    }
}

void Library::remove(const std::string& id) {
    const LibNode* n = node(id);
    if (!n || n->isSystem()) return;
    std::set<std::string> gone{id};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& x : nodes)
            if (!gone.count(x.id) && gone.count(x.parent)) {
                gone.insert(x.id);
                grew = true;
            }
    }
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const LibNode& x) { return gone.count(x.id) > 0; }),
                nodes.end());
}

std::string Library::recordingFolderName(const std::string& project, int day, int month, int year) {
    char d[16];
    std::snprintf(d, sizeof d, "%02d.%02d.%04d", day, month, year);
    const std::string p = trim(project);
    return (p.empty() ? std::string("Project") : p) + " " + d;
}

std::string Library::recordingName(const std::string& base, int h, int m, int s) {
    char t[16];
    std::snprintf(t, sizeof t, "%02d-%02d-%02d", h, m, s);
    return base + " " + t;
}

std::string Library::recordingsFolder(const std::string& name) {
    for (const auto& n : nodes)
        if (n.parent == recordings && n.kind == LibKind::folder && n.name == name) return n.id;
    return createFolder(recordings, name).id;
}

// MARK: — JSON (как у iPad: nodes, marks)

static const char* kindName(LibKind k) {
    switch (k) {
        case LibKind::folder: return "folder";
        case LibKind::sample: return "sample";
        case LibKind::preset: return "preset";
        case LibKind::table: return "table";
    }
    return "folder";
}

std::string LibraryCoding::encode(const Library& lib) {
    Json nodes = Json::array();
    for (const auto& n : lib.nodes) {
        Json j;
        j["id"] = n.id;
        j["parent"] = n.parent;
        j["kind"] = kindName(n.kind);
        j["name"] = n.name;
        if (n.key) j["key"] = *n.key;
        if (n.preset) j["preset"] = Json::parse(ProjectCoding::encode(*n.preset));
        if (n.system) j["system"] = *n.system;
        j["updated"] = n.updated;
        if (n.order) j["order"] = *n.order;
        nodes.push_back(std::move(j));
    }
    Json marks = Json::object();
    for (const auto& [k, v] : lib.marks) marks[k] = v;
    Json out;
    out["nodes"] = std::move(nodes);
    out["marks"] = std::move(marks);
    return out.dump();
}

static bool getNode(const Json& j, LibNode& n) {
    if (!j.is_object()) return false;
    std::string kind;
    if (!json::req(j, "id", n.id) || !json::req(j, "parent", n.parent) || !json::req(j, "kind", kind) ||
        !json::req(j, "name", n.name) || !json::req(j, "updated", n.updated))
        return false;
    if (kind == "folder") n.kind = LibKind::folder;
    else if (kind == "sample") n.kind = LibKind::sample;
    else if (kind == "preset") n.kind = LibKind::preset;
    else if (kind == "table") n.kind = LibKind::table;
    else return false;
    if (!json::ifPresent(j, "key", n.key) || !json::ifPresent(j, "system", n.system) ||
        !json::ifPresent(j, "order", n.order))
        return false;
    if (const auto it = j.find("preset"); it != j.end() && !it->is_null()) {
        auto p = ProjectCoding::decodeSynthPreset(it->dump());
        if (!p) return false;
        n.preset = *p;
    }
    return true;
}

std::optional<Library> LibraryCoding::decode(const std::string& data) {
    const Json j = Json::parse(data, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    Library lib;
    lib.nodes.clear();
    // Как c.value: не читается массив — пустой.
    if (const auto it = j.find("nodes"); it != j.end() && it->is_array()) {
        std::vector<LibNode> list;
        bool ok = true;
        for (const auto& x : *it) {
            LibNode n;
            if (!getNode(x, n)) {
                ok = false;
                break;
            }
            list.push_back(std::move(n));
        }
        if (ok) lib.nodes = std::move(list);
    }
    if (const auto it = j.find("marks"); it != j.end() && it->is_object())
        for (auto m = it->begin(); m != it->end(); ++m) lib.marks[m.key()] = m.value();
    lib.ensureRecordings();
    return lib;
}

Library LibraryStore::load() const {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Library();
    std::ostringstream ss;
    ss << f.rdbuf();
    auto lib = LibraryCoding::decode(ss.str());
    return lib ? *lib : Library();
}

bool LibraryStore::save(const Library& lib) const {
    const std::string data = LibraryCoding::encode(lib);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), std::streamsize(data.size()));
        f.flush();
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

}  // namespace jambo
