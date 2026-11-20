#include "MasterPresets.h"
#include <algorithm>
#include <cctype>
#include <set>
#include "Json.h"
#include "master/MasterChain.h"

namespace jambo {

const std::array<const char*, 4> MasterPresetBook::prefixes = {"sat.", "cmp.", "clip.", "lim."};
const std::array<const char*, 4> MasterPresetBook::titles = {"SAT", "COMP", "CLIP", "LIM"};

std::vector<std::string> MasterPresetBook::ids(int m) {
    const std::string pre = prefixes[size_t(std::max(0, std::min(3, m)))];
    std::vector<std::string> out;
    for (const auto& [id, _] : MasterChain::ids())
        if (id.rfind(pre, 0) == 0) out.push_back(id);
    return out;
}

const std::vector<MasterPreset>& MasterPresetBook::list(int m) const {
    static const std::vector<MasterPreset> none;
    return m >= 0 && m < 4 ? lists[size_t(m)] : none;
}

std::string MasterPresetBook::nextName(int m) const {
    std::set<std::string> names;
    for (const auto& p : list(m)) names.insert(p.name);
    int k = int(list(m).size()) + 1;
    const std::string t = titles[size_t(std::max(0, std::min(3, m)))];
    while (names.count(t + " " + std::to_string(k))) k++;
    return t + " " + std::to_string(k);
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

std::optional<std::string> MasterPresetBook::clean(const std::string& name) {
    size_t a = 0, b = name.size();
    while (a < b && name[a] == ' ') a++;
    while (b > a && name[b - 1] == ' ') b--;
    std::string t = prefixChars(name.substr(a, b - a), nameMax);
    if (t.empty()) return std::nullopt;
    return t;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::optional<int> MasterPresetBook::index(int m, const std::string& name, int except) const {
    const auto n = clean(name);
    if (!n) return std::nullopt;
    const auto& l = list(m);
    for (int k = 0; k < int(l.size()); k++)
        if (k != except && lower(l[size_t(k)].name) == lower(*n)) return k;
    return std::nullopt;
}

void MasterPresetBook::save(int m, const std::string& name, const ParamMap& master, std::optional<int> over) {
    if (m < 0 || m >= 4) return;
    const ParamMap defs = MasterChain::defaultValues();
    ParamMap v;
    for (const auto& id : ids(m)) {
        const auto a = master.find(id);
        const auto d = defs.find(id);
        v[id] = a != master.end() ? a->second : d != defs.end() ? d->second : 0;
    }
    MasterPreset p{clean(name).value_or(nextName(m)), v};
    auto& l = lists[size_t(m)];
    if (over && *over >= 0 && *over < int(l.size())) l[size_t(*over)] = p;
    else l.push_back(p);
}

void MasterPresetBook::rename(int m, int k, const std::string& name) {
    const auto n = clean(name);
    if (m < 0 || m >= 4 || k < 0 || k >= int(lists[size_t(m)].size()) || !n) return;
    lists[size_t(m)][size_t(k)].name = *n;
}

void MasterPresetBook::remove(int m, int k) {
    if (m < 0 || m >= 4 || k < 0 || k >= int(lists[size_t(m)].size())) return;
    lists[size_t(m)].erase(lists[size_t(m)].begin() + k);
}

void MasterPresetBook::move(int m, int from, int to) {
    if (m < 0 || m >= 4) return;
    auto& l = lists[size_t(m)];
    const int n = int(l.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    const MasterPreset p = l[size_t(from)];
    l.erase(l.begin() + from);
    l.insert(l.begin() + to, p);
}

void MasterPresetBook::apply(int m, int k, ParamMap& master) const {
    if (k < 0 || k >= int(list(m).size())) return;
    const MasterPreset& p = lists[size_t(m)][size_t(k)];
    const ParamMap defs = MasterChain::defaultValues();
    for (const auto& id : ids(m)) {
        if (const auto it = p.values.find(id); it != p.values.end()) master[id] = it->second;
        else if (const auto d = defs.find(id); d != defs.end()) master[id] = d->second;
    }
}

MasterPresetBook MasterPresetBook::decode(const std::string& data) {
    MasterPresetBook b;
    const Json j = Json::parse(data, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return b;
    const auto it = j.find("lists");
    if (it == j.end() || !it->is_array()) return b;
    MasterPresetBook out;
    size_t m = 0;
    for (const auto& l : *it) {
        if (m >= 4) break;
        if (!l.is_array()) return b;
        for (const auto& p : l) {
            MasterPreset x;
            if (!p.is_object() || !json::req(p, "name", x.name) || !json::req(p, "values", x.values)) return b;
            out.lists[m].push_back(x);
        }
        m++;
    }
    return out;
}

std::string MasterPresetBook::encode() const {
    Json lists_ = Json::array();
    for (const auto& l : lists) {
        Json a = Json::array();
        for (const auto& p : l) {
            Json v = Json::object();
            for (const auto& [k, x] : p.values) v[k] = x;
            a.push_back(Json{{"name", p.name}, {"values", v}});
        }
        lists_.push_back(a);
    }
    return Json{{"lists", lists_}}.dump();
}

}  // namespace jambo
