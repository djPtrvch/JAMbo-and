// Порт ProjectFormat (Core/Model/ProjectStore.swift, ветка swift JAMbo) и JSON отдельных записей.
#include "ProjectStore.h"
#include "Json.h"

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
}  // namespace jambo
