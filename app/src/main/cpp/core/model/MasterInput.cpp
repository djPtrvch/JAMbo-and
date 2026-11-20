#include "MasterInput.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include "master/MasterChain.h"

namespace jambo {
namespace MasterInput {

static bool endsWith(const std::string& s, const char* t) {
    const std::string x(t);
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

bool isTime(const ParamDef& p) { return endsWith(p.id, ".attack") || endsWith(p.id, ".release") || endsWith(p.id, ".lookahead"); }

std::optional<double> offValue(const ParamDef& p) {
    if (p.offBottom) return 0.0;
    if (p.id == "sat.focus" || p.id == "cmp.schpf") return p.min;
    return std::nullopt;
}

static std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

std::string text(const ParamDef& p, double v) {
    if (!p.options.empty()) return p.format(v);
    if (p.autoTop && v >= MasterChain::autoValue) return "auto";
    if (auto off = offValue(p); off && v <= *off + 0.5) return "off";
    if (p.step && *p.step < 0.1) return fmt("%.2f", v);
    if (p.step || std::fabs(v) < 10) return fmt("%.1f", v);
    return std::to_string((long long)std::llround(v));
}

std::optional<double> parse(const ParamDef& p, const std::string& text) {
    std::string s;
    for (char c : text) s += c == ',' ? '.' : char(std::tolower((unsigned char)c));
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (!p.options.empty())
        for (size_t k = 0; k < p.options.size(); k++) {
            std::string o;
            for (char c : p.options[k]) o += char(std::tolower((unsigned char)c));
            if (o == s) return double(k);
        }
    if (p.autoTop && s.rfind("auto", 0) == 0) return MasterChain::autoValue;
    if (auto off = offValue(p); off && s == "off") return *off;
    std::string digits;
    size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) digits += s[i++];
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) digits += s[i++];
    char* end = nullptr;
    double v = std::strtod(digits.c_str(), &end);
    if (digits.empty() || !end || *end != 0 || digits == "-" || digits == "+") return std::nullopt;
    std::string unit = s.substr(i);
    while (!unit.empty() && unit.front() == ' ') unit.erase(unit.begin());
    if (isTime(p) && !unit.empty() && unit[0] == 's') v *= 1000;
    if (!unit.empty() && unit[0] == 'k') v *= 1000;
    return clamp(p, v);
}

double clamp(const ParamDef& p, double v) {
    if (p.autoTop && v >= MasterChain::autoValue) return MasterChain::autoValue;
    if (p.offBottom && v <= 0) return 0;
    double x = std::min(p.max, std::max(p.min, v));
    if (p.kind == KnobKind::stepped) x = std::round(x);
    if (p.step) x = std::round(x / *p.step) * *p.step;
    return x;
}

}  // namespace MasterInput
}  // namespace jambo
