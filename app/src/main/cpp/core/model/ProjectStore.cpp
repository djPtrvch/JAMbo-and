// Порт ProjectFormat (Core/Model/ProjectStore.swift, ветка swift JAMbo) и JSON отдельных записей.
#include "ProjectStore.h"
#include "Json.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace jambo {
namespace json {

bool get(const Json& j, ProjectInfo& v) {
    if (!j.is_object()) return false;
    v.id = value(j, "id", std::string());
    v.name = value(j, "name", std::string());
    v.updated = value(j, "updated", 0.0);
    v.projectId = opt<std::string>(j, "projectId");
    v.projectName = opt<std::string>(j, "projectName");
    v.bpm = value(j, "bpm", 120.0);
    v.root = value(j, "root", 0);
    v.scale = value(j, "scale", 0);
    return true;
}

Json put(const ProjectInfo& v) {
    Json o = Json::object();
    o["id"] = put(v.id);
    o["name"] = put(v.name);
    o["updated"] = put(v.updated);
    putIf(o, "projectId", v.projectId);
    putIf(o, "projectName", v.projectName);
    o["bpm"] = put(v.bpm);
    o["root"] = put(v.root);
    o["scale"] = put(v.scale);
    return o;
}

/// Синтезированный Decodable ProjectFile: все поля обязательны.
bool get(const Json& j, ProjectFile& v) {
    return j.is_object() && req(j, "format", v.format) && req(j, "version", v.version) && req(j, "info", v.info) &&
           req(j, "doc", v.doc);
}

}  // namespace json

static std::optional<Json> parse(const std::string& data) {
    Json j = Json::parse(data, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

/// Как JSONEncoder: компактно; неверный UTF-8 в строках — заменой, без исключений.
static std::string dump(const Json& j) { return j.dump(-1, ' ', false, Json::error_handler_t::replace); }

ProjectFile ProjectFile::make(const ProjectInfo& info, const Project& doc) {
    ProjectFile f;
    f.format = ProjectFormat::name;
    f.version = ProjectFormat::version;
    f.info = info;
    f.info.bpm = doc.bpm;
    f.info.root = doc.root;
    f.info.scale = doc.scale;
    f.doc = doc;
    return f;
}

namespace ProjectFormat {

const char* const name = "jambo-swift";

std::optional<std::string> encode(const ProjectFile& f) {
    Json o = Json::object();
    o["format"] = json::put(f.format);
    o["version"] = json::put(f.version);
    o["info"] = json::put(f.info);
    o["doc"] = json::put(f.doc);
    return dump(o);
}

std::optional<ProjectFile> decode(const std::string& data) {
    const auto j = parse(data);
    if (!j || !j->is_object()) return std::nullopt;
    std::string format;
    int ver = 0;
    if (!json::req(*j, "format", format) || !json::req(*j, "version", ver) || format != name) return std::nullopt;
    // Прежних версий формата файла пока нет (версия 1 — первая): миграции нет.
    ProjectFile f;
    if (!json::get(*j, f)) return std::nullopt;
    f.version = version;
    f.doc.repair();
    return f;
}

}  // namespace ProjectFormat

namespace ProjectCoding {

std::string encode(const KnobDefaults& k) { return dump(json::put(k)); }

KnobDefaults decodeKnobDefaults(const std::string& data) {
    const auto j = parse(data);
    KnobDefaults k;
    if (!j || !json::get(*j, k)) return KnobDefaults();
    if (!k.filterV) {
        k.migrateFilter();
        k.filterV = 2;
    }
    return k;
}

std::string encode(const SynthPreset& p) { return dump(json::put(p)); }

std::optional<SynthPreset> decodeSynthPreset(const std::string& data) {
    const auto j = parse(data);
    SynthPreset p;
    if (!j || !json::get(*j, p)) return std::nullopt;
    return p;
}

}  // namespace ProjectCoding
// MARK: — ProjectStore

namespace fs = std::filesystem;

const char* const ProjectStore::autosaveId = "autosave";

ProjectStore::ProjectStore(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

static std::string base36(uint64_t v) {
    if (v == 0) return "0";
    std::string out;
    while (v > 0) {
        const int d = int(v % 36);
        out.insert(out.begin(), char(d < 10 ? '0' + d : 'a' + d - 10));
        v /= 36;
    }
    return out;
}

std::string ProjectStore::newId() {
    using namespace std::chrono;
    const auto ms = uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    static std::mt19937_64 g{std::random_device{}()};
    return "p" + base36(ms) + base36(g() % (36 * 36 * 36 * 36));
}

std::optional<std::string> ProjectStore::path(const std::string& id) const {
    if (id.empty()) return std::nullopt;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return std::nullopt;
    return (fs::path(dir_) / (id + ".json")).string();
}

static std::optional<std::string> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<ProjectInfo> ProjectStore::list() const {
    std::vector<ProjectInfo> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.size() <= 5 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
        const std::string id = n.substr(0, n.size() - 5);
        const auto p = path(id);
        if (!p) continue;
        const auto data = readFile(*p);
        if (!data) continue;
        // Только формат и запись списка (ListHead), без разбора содержимого проекта.
        const auto j = parse(*data);
        if (!j || !j->is_object()) continue;
        std::string format;
        ProjectInfo info;
        if (!json::req(*j, "format", format) || format != ProjectFormat::name || !json::req(*j, "info", info)) continue;
        info.id = id;
        out.push_back(info);
    }
    std::stable_sort(out.begin(), out.end(), [](const ProjectInfo& a, const ProjectInfo& b) { return a.updated > b.updated; });
    return out;
}

std::optional<ProjectFile> ProjectStore::load(const std::string& id) const {
    const auto p = path(id);
    if (!p) return std::nullopt;
    const auto data = readFile(*p);
    if (!data) return std::nullopt;
    auto f = ProjectFormat::decode(*data);
    if (!f) return std::nullopt;
    f->info.id = id;
    return f;
}

bool ProjectStore::save(const Project& doc, const ProjectInfo& info) const {
    const auto p = path(info.id);
    if (!p) return false;
    const auto data = ProjectFormat::encode(ProjectFile::make(info, doc));
    if (!data) return false;
    const std::string tmp = *p + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data->data(), std::streamsize(data->size()));
        f.flush();
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, *p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

void ProjectStore::remove(const std::string& id) const {
    const auto p = path(id);
    if (!p) return;
    std::error_code ec;
    fs::remove(*p, ec);
}

bool ProjectStore::rename(const std::string& id, const std::string& name) const {
    auto f = load(id);
    if (!f) return false;
    f->info.name = name;
    return save(f->doc, f->info);
}

std::optional<ProjectInfo> ProjectStore::find(const std::string& name) const {
    for (const auto& i : list())
        if (i.id != autosaveId && i.name == name) return i;
    return std::nullopt;
}

}  // namespace jambo
