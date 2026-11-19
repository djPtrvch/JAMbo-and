// Headphone Monitor: расчёт параметров цепочки (порт Core/Monitor/MonitorDesign.swift).
#include "MonitorDesign.h"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include "RoomIR.h"
#include "eq/EqCoeffs.h"
#include "eq/EqSnapshot.h"

namespace jambo {

std::vector<TranslationPreset> TranslationPreset::list(const std::string& json) {
    std::vector<TranslationPreset> out;
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object() || !root.contains("presets") || !root["presets"].is_array()) return out;
    for (const auto& p : root["presets"]) {
        if (!p.is_object() || !p.contains("name") || !p["name"].is_string()) continue;
        TranslationPreset tp;
        tp.name = p["name"].get<std::string>();
        if (p.contains("mono") && p["mono"].is_boolean()) tp.mono = p["mono"].get<bool>();
        if (p.contains("room") && p["room"].is_string()) tp.room = p["room"].get<std::string>();
        if (p.contains("filters") && p["filters"].is_array()) {
            for (const auto& f : p["filters"]) {
                if (!f.is_object() || !f.contains("type") || !f["type"].is_string() || !f.contains("freq") ||
                    !f["freq"].is_number())
                    continue;
                Filter fl;
                fl.type = f["type"].get<std::string>();
                fl.freq = f["freq"].get<double>();
                if (f.contains("gain") && f["gain"].is_number()) fl.gain = f["gain"].get<double>();
                if (f.contains("q") && f["q"].is_number()) fl.q = f["q"].get<double>();
                if (f.contains("order") && f["order"].is_number()) fl.order = f["order"].get<int>();
                tp.filters.push_back(fl);
            }
        }
        out.push_back(tp);
    }
    return out;
}

namespace MonitorDesign {

std::vector<MonitorBiquad> biquads(const TranslationPreset* p) {
    std::vector<MonitorBiquad> out;
    if (!p) return out;
    for (const auto& f : p->filters) {
        std::string code = f.type;
        for (auto& c : code) c = char(std::toupper(static_cast<unsigned char>(c)));
        const double q = f.q.value_or(0.7071067811865476);
        const double g = f.gain.value_or(0);
        if (code == "HP" || code == "LP") {
            const int t = code == "HP" ? EqSpec::lowCut : EqSpec::highCut;
            if (f.order.value_or(2) >= 4) {
                for (double bq : MonitorSpec::butter4) out.push_back({t, f.freq, 0, bq});
            } else {
                out.push_back({t, f.freq, 0, q});
            }
        } else if (code == "PK") {
            out.push_back({EqSpec::bell, f.freq, g, q});
        } else if (code == "LS" || code == "LSC") {
            out.push_back({EqSpec::lowShelf, f.freq, g, q});
        } else if (code == "HS" || code == "HSC") {
            out.push_back({EqSpec::highShelf, f.freq, g, q});
        }
    }
    return out;
}

std::vector<MonitorBiquad> correction(const Eq* eq, double s) {
    std::vector<MonitorBiquad> out;
    if (!eq || s <= 0) return out;
    for (const auto& ob : eq->bands) {
        if (!ob || !ob->on) continue;
        const EqBand& b = *ob;
        if (EqSpec::hasGain(b.type)) {
            const double g = std::max(-MonitorSpec::filterMax, std::min(MonitorSpec::filterMax, b.gain)) * s;
            if (g == 0) continue;
            const double q = b.type == EqSpec::bell ? b.q : 0.7071067811865476;
            out.push_back({b.type, b.freq, g, q});
        } else {
            for (int k = 0; k < EqSpec::stages(b); ++k) out.push_back({b.type, b.freq, 0, b.q});
        }
    }
    return out;
}

void coeffs(const MonitorBiquad& b, double sr, double* out) {
    if (EqSpec::hasGain(b.type) && b.gain == 0) {
        identity(out);
        return;
    }
    const double q = EqSpec::isCut(b.type) ? 20 * std::log10(b.q) : b.q;
    EqCoeffs::coeffs(b.type, b.freq, b.gain, q, sr, out);
}

std::vector<double> make(const Eq* eq, const MonitorSettings& m, const Eq* correct, const TranslationPreset* preset,
                         const RoomResponse* room, double ambience, double sr) {
    std::vector<double> out(MonitorSpec::size, 0.0);
    double* base = out.data();
    for (int k = 0; k < MonitorSpec::sections; ++k) identity(base + k * 5);
    const auto trans = biquads(preset);
    for (size_t k = 0; k < trans.size() && k < size_t(MonitorSpec::transSlots); ++k) coeffs(trans[k], sr, base + k * 5);
    if (m.sub) {
        for (int k = 0; k < 2; ++k) {
            const MonitorBiquad b{EqSpec::highCut, MonitorSpec::subHz, 0, MonitorSpec::butter4[k]};
            coeffs(b, sr, base + (MonitorSpec::transSlots + k) * 5);
        }
    }
    const int c0 = MonitorSpec::transSlots + MonitorSpec::subSlots;
    const auto corr = correction(eq, m.strength);
    for (size_t k = 0; k < corr.size() && k < size_t(MonitorSpec::corrSlots); ++k)
        coeffs(corr[k], sr, base + (c0 + int(k)) * 5);
    const int u0 = c0 + MonitorSpec::corrSlots;
    const auto user = correction(correct, 1);
    for (size_t k = 0; k < user.size() && k < size_t(MonitorSpec::userSlots); ++k)
        coeffs(user[k], sr, base + (u0 + int(k)) * 5);
    base[MonitorSpec::monoAt] = (m.mono || (preset && preset->mono.value_or(false))) ? 1 : 0;
    base[MonitorSpec::onAt] = (eq ? eq->on : true) ? 1 : 0;
    const double db = compensation(base, sr, room, ambience);
    base[MonitorSpec::compAt] = db == 0 ? 1 : std::pow(10.0, db / 20);
    return out;
}

double power(const double* c, double w) {
    const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    const double nr = c[0] + c[1] * c1 + c[2] * c2;
    const double ni = -(c[1] * s1 + c[2] * s2);
    const double dr = 1 + c[3] * c1 + c[4] * c2;
    const double di = -(c[3] * s1 + c[4] * s2);
    return (nr * nr + ni * ni) / std::max(1e-30, dr * dr + di * di);
}

double compensation(const double* p, double sr, const RoomResponse* room, double amb) {
    std::vector<int> active;
    for (int k = 0; k < MonitorSpec::sections; ++k)
        if (!isIdentity(p + k * 5)) active.push_back(k);
    if (active.empty() && !room) return 0;
    double sum = 0;
    for (int i = 0; i < points; ++i) {
        const double w = 2 * M_PI * pointHz(i) / sr;
        double pw = 1;
        for (int k : active) pw *= power(p + k * 5, w);
        if (room) pw *= room->power(i, amb);
        sum += pw;
    }
    const double mean = sum / double(points);
    if (mean <= 0) return MonitorSpec::compMax;
    const double db = -10 * std::log10(mean);
    if (std::abs(db) < 0.005) return 0;
    return std::max(-MonitorSpec::compMax, std::min(MonitorSpec::compMax, db));
}

double response(const std::vector<double>& p, double hz, double sr) {
    if (p.size() < size_t(MonitorSpec::size)) return 0;
    const double w = 2 * M_PI * hz / sr;
    double pw = 1;
    for (int k = 0; k < MonitorSpec::sections; ++k) pw *= power(p.data() + k * 5, w);
    return 10 * std::log10(std::max(1e-30, pw));
}

}  // namespace MonitorDesign
}  // namespace jambo
