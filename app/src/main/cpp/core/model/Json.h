// Чтение и запись JSON по правилам Swift Codable (JSONDecoder / JSONEncoder Swift-версии JAMbo):
// - get(j, out) — значение данного типа; false — не тот тип (в Swift — ошибка decode);
// - value(c, k, def) — поле или значение по умолчанию (нет, null или не тот тип; c.value в ProjectCoding.swift);
// - opt(c, k) — необязательное поле (нет, null или не тот тип — nullopt; c.optional);
// - req(c, k, out) — обязательное поле (decode: нет, null или не тот тип — ошибка всего объекта);
// - ifPresent(c, k, out) — decodeIfPresent: нет или null — nullopt, не тот тип — ошибка всего объекта.
// Double: число или строки "inf", "-inf", "nan" (nonConformingFloat… в ProjectFormat). Int — целое число или дробное с
// точным целым значением; Bool — только true/false; строка — только строка.
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace jambo {

struct ModAssign; struct Step; struct Clip; struct SceneRow; struct Track; struct Project; struct EqBand; struct Eq;
struct Folder; struct SliceMark; struct FolderItem; struct SynthPreset; struct WarpMark; struct WarpInfo;
struct TrackMidi; struct TrackArp; struct ArrAutoPoint; struct ArrAutoLane; struct ArrBlock; struct ArrMarker;
struct ArrRef; struct Arrangement; struct TrackSound; struct KnobDefaults; struct ProjectInfo; struct ProjectFile;

using Json = nlohmann::ordered_json;

namespace json {

inline bool get(const Json& j, bool& v) {
    if (!j.is_boolean()) return false;
    v = j.get<bool>();
    return true;
}

inline bool getInt64(const Json& j, long long& v) {
    if (j.is_number_integer()) {
        v = j.get<long long>();
        return true;
    }
    if (j.is_number_unsigned()) {
        const auto u = j.get<unsigned long long>();
        if (u > (unsigned long long)std::numeric_limits<long long>::max()) return false;
        v = (long long)u;
        return true;
    }
    if (j.is_number_float()) {
        const double d = j.get<double>();
        if (!std::isfinite(d) || d != std::floor(d) || d < -9.2e18 || d > 9.2e18) return false;
        v = (long long)d;
        return true;
    }
    return false;
}

/// Int Swift (64 бита) → int: вне пределов int — насыщение (дальше значения всё равно ограничиваются repair).
inline bool get(const Json& j, int& v) {
    long long x;
    if (!getInt64(j, x)) return false;
    v = int(std::max<long long>(std::numeric_limits<int>::min(), std::min<long long>(std::numeric_limits<int>::max(), x)));
    return true;
}

/// Необязательное поле Int32 Swift (c.optional): вне пределов — nullopt.
inline std::optional<int32_t> optInt32(const Json& c, const char* k) {
    const auto it = c.find(k);
    long long x;
    if (it == c.end() || !getInt64(*it, x) || x < INT32_MIN || x > INT32_MAX) return std::nullopt;
    return int32_t(x);
}

inline bool get(const Json& j, double& v) {
    if (j.is_number()) {
        v = j.get<double>();
        return true;
    }
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if (s == "inf") v = std::numeric_limits<double>::infinity();
        else if (s == "-inf") v = -std::numeric_limits<double>::infinity();
        else if (s == "nan") v = std::numeric_limits<double>::quiet_NaN();
        else return false;
        return true;
    }
    return false;
}

inline bool get(const Json& j, std::string& v) {
    if (!j.is_string()) return false;
    v = j.get<std::string>();
    return true;
}

// Структуры модели — в ProjectCoding.cpp; объявлены до шаблонов, чтобы их видели массивы и словари.
#define JAMBO_JSON_TYPE(T) \
    bool get(const Json& j, T& v); \
    Json put(const T& v);
JAMBO_JSON_TYPE(ModAssign)
JAMBO_JSON_TYPE(Step)
JAMBO_JSON_TYPE(Clip)
JAMBO_JSON_TYPE(SceneRow)
JAMBO_JSON_TYPE(Track)
JAMBO_JSON_TYPE(Project)
JAMBO_JSON_TYPE(EqBand)
JAMBO_JSON_TYPE(Eq)
JAMBO_JSON_TYPE(Folder)
JAMBO_JSON_TYPE(SliceMark)
JAMBO_JSON_TYPE(FolderItem)
JAMBO_JSON_TYPE(SynthPreset)
JAMBO_JSON_TYPE(WarpMark)
JAMBO_JSON_TYPE(WarpInfo)
JAMBO_JSON_TYPE(TrackMidi)
JAMBO_JSON_TYPE(TrackArp)
JAMBO_JSON_TYPE(ArrAutoPoint)
JAMBO_JSON_TYPE(ArrAutoLane)
JAMBO_JSON_TYPE(ArrBlock)
JAMBO_JSON_TYPE(ArrMarker)
JAMBO_JSON_TYPE(ArrRef)
JAMBO_JSON_TYPE(Arrangement)
JAMBO_JSON_TYPE(TrackSound)
JAMBO_JSON_TYPE(KnobDefaults)
JAMBO_JSON_TYPE(ProjectInfo)
bool get(const Json& j, ProjectFile& v);
#undef JAMBO_JSON_TYPE

template <class T> bool get(const Json& j, std::vector<T>& v);
template <class T> bool get(const Json& j, std::optional<T>& v);
template <class T> bool get(const Json& j, std::map<std::string, T>& v);

/// Элемент массива [T?] или значение словаря [String: T?]: null — nil.
template <class T> bool get(const Json& j, std::optional<T>& v) {
    if (j.is_null()) {
        v.reset();
        return true;
    }
    T x{};
    if (!get(j, x)) return false;
    v = std::move(x);
    return true;
}

template <class T> bool get(const Json& j, std::vector<T>& v) {
    if (!j.is_array()) return false;
    std::vector<T> out;
    out.reserve(j.size());
    for (const auto& e : j) {
        T x{};
        if (!get(e, x)) return false;
        out.push_back(std::move(x));
    }
    v = std::move(out);
    return true;
}

template <class T> bool get(const Json& j, std::map<std::string, T>& v) {
    if (!j.is_object()) return false;
    std::map<std::string, T> out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        T x{};
        if (!get(it.value(), x)) return false;
        out[it.key()] = std::move(x);
    }
    v = std::move(out);
    return true;
}

/// Поле k объекта c; nullptr — нет или null.
inline const Json* field(const Json& c, const char* k) {
    const auto it = c.find(k);
    return it == c.end() || it->is_null() ? nullptr : &*it;
}

template <class T> T value(const Json& c, const char* k, T def) {
    const Json* f = field(c, k);
    T x{};
    if (f && get(*f, x)) return x;
    return def;
}

template <class T> std::optional<T> opt(const Json& c, const char* k) {
    const Json* f = field(c, k);
    T x{};
    if (f && get(*f, x)) return x;
    return std::nullopt;
}

template <class T> bool req(const Json& c, const char* k, T& out) {
    const Json* f = field(c, k);
    return f && get(*f, out);
}

template <class T> bool ifPresent(const Json& c, const char* k, std::optional<T>& out) {
    const Json* f = field(c, k);
    if (!f) {
        out.reset();
        return true;
    }
    T x{};
    if (!get(*f, x)) return false;
    out = std::move(x);
    return true;
}

// MARK: — запись

inline Json put(bool v) { return Json(v); }
inline Json put(int v) { return Json(v); }
/// −∞, +∞ и NaN — строками.
inline Json put(double v) {
    if (std::isnan(v)) return Json("nan");
    if (std::isinf(v)) return Json(v > 0 ? "inf" : "-inf");
    return Json(v);
}
inline Json put(const std::string& v) { return Json(v); }

template <class T> Json put(const std::vector<T>& v);
template <class T> Json put(const std::optional<T>& v);
template <class T> Json put(const std::map<std::string, T>& v);

/// Элемент [T?] или значение [String: T?]: nil — null.
template <class T> Json put(const std::optional<T>& v) { return v ? put(*v) : Json(nullptr); }

template <class T> Json put(const std::vector<T>& v) {
    Json a = Json::array();
    for (const auto& e : v) a.push_back(put(e));
    return a;
}

template <class T> Json put(const std::map<std::string, T>& v) {
    Json o = Json::object();
    for (const auto& kv : v) o[kv.first] = put(kv.second);
    return o;
}

/// encodeIfPresent: поле — только если есть.
template <class T> void putIf(Json& o, const char* k, const std::optional<T>& v) {
    if (v) o[k] = put(*v);
}

}  // namespace json
}  // namespace jambo
