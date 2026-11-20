#include "EqPresets.h"
#include <algorithm>
#include <cctype>
#include <set>
#include "Json.h"
#include "MasterPresets.h"

namespace jambo {
namespace json {
Json put(const EqBand& v);
bool get(const Json& j, EqBand& v);
}  // namespace json

int EqPreset::count() const {
    return int(std::count_if(bands.begin(), bands.end(), [](const auto& b) { return b.has_value(); }));
}

std::string EqPresetBook::nextName() const {
    std::set<std::string> names;
    for (const auto& p : list) names.insert(p.name);
    int k = int(list.size()) + 1;
    while (names.count("EQ " + std::to_string(k))) k++;
    return "EQ " + std::to_string(k);
}

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::optional<int> EqPresetBook::index(const std::string& name, int except) const {
    const auto n = MasterPresetBook::clean(name);
    if (!n) return std::nullopt;
    for (int k = 0; k < int(list.size()); k++)
        if (k != except && lower(list[size_t(k)].name) == lower(*n)) return k;
    return std::nullopt;
}

void EqPresetBook::save(const std::string& name, const Eq& eq, std::optional<int> over) {
    std::vector<std::optional<EqBand>> bands = eq.bands;
    while (int(bands.size()) < EqSpec::bands) bands.push_back(std::nullopt);
    bands.resize(size_t(EqSpec::bands));
    EqPreset p{MasterPresetBook::clean(name).value_or(nextName()), bands};
    if (over && *over >= 0 && *over < int(list.size())) list[size_t(*over)] = p;
    else list.push_back(p);
}

void EqPresetBook::rename(int k, const std::string& name) {
    const auto n = MasterPresetBook::clean(name);
    if (k < 0 || k >= int(list.size()) || !n) return;
    list[size_t(k)].name = *n;
}

void EqPresetBook::remove(int k) {
    if (k < 0 || k >= int(list.size())) return;
    list.erase(list.begin() + k);
}

void EqPresetBook::move(int from, int to) {
    const int n = int(list.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    const EqPreset p = list[size_t(from)];
    list.erase(list.begin() + from);
    list.insert(list.begin() + to, p);
}

EqPresetBook EqPresetBook::decode(const std::string& data) {
    EqPresetBook b;
    const Json j = Json::parse(data, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return b;
    const auto it = j.find("list");
    if (it == j.end() || !it->is_array()) return b;
    EqPresetBook out;
    for (const auto& p : *it) {
        EqPreset x;
        if (!p.is_object() || !json::req(p, "name", x.name)) return b;
        const auto bs = p.find("bands");
        if (bs == p.end() || !bs->is_array()) return b;
        for (const auto& e : *bs) {
            if (e.is_null()) {
                x.bands.push_back(std::nullopt);
                continue;
            }
            EqBand band;
            if (!json::get(e, band)) return b;
            x.bands.push_back(band);
        }
        out.list.push_back(x);
    }
    return out;
}

std::string EqPresetBook::encode() const {
    Json l = Json::array();
    for (const auto& p : list) {
        Json bs = Json::array();
        for (const auto& b : p.bands) bs.push_back(b ? json::put(*b) : Json(nullptr));
        l.push_back(Json{{"name", p.name}, {"bands", bs}});
    }
    return Json{{"list", l}}.dump();
}

}  // namespace jambo
