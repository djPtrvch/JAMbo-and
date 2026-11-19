// Помощники таблиц параметров (Params.cpp, ParamsFx.cpp); только для этих файлов.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "Params.h"

namespace jambo {
namespace ParamsUtil {

/// String(format:) Swift.
inline std::string sf(const char* f, ...) {
    char b[64];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

/// "\(Int(v.rounded()))" Swift: округление от нуля.
inline std::string ri(double v) { return std::to_string((long long)std::round(v)); }

inline ParamDef smooth(const char* id, const char* label, double min, double max, double def, ParamDef::Format f) {
    return ParamDef(id, label, min, max, def, std::move(f));
}

inline ParamDef stepped(const std::string& id, const std::string& label, const std::vector<std::string>& opts, double def = 0) {
    return ParamDef(id, label, 0, double(opts.size()) - 1, def,
                    [opts](double v) {
                        const double i = std::round(v);
                        return i >= 0 && i < double(opts.size()) ? opts[size_t(i)] : std::string();
                    })
        .withKind(KnobKind::stepped)
        .withOptions(opts);
}

inline ParamDef master(const char* id, const char* label, double min, double max, double def, ParamDef::Format f) {
    return ParamDef(id, label, min, max, def, std::move(f)).withNoLock();
}

inline std::vector<ParamDef> rowsToGrid(const std::vector<std::vector<ParamDef>>& cols) {
    std::vector<ParamDef> g;
    for (int i = 0; i < 16; ++i) g.push_back(cols[size_t(i / 4)][size_t(i % 4)]);
    return g;
}

}  // namespace ParamsUtil
}  // namespace jambo
