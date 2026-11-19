// P-lock ноты без коллекций (порт NoteLocks, Core/Engine/CoreEvent.swift, ветка swift JAMbo): маска по VoiceParam и
// значения.
#pragma once
#include <map>
#include <string>
#include "VoiceParam.h"

namespace jambo {

struct NoteLocks {
    VoiceMask mask = 0;
    float vals[VoiceParam::count] = {};

    NoteLocks() = default;
    /// Ручки пресета (id → значение) в p-lock голоса (основной поток).
    explicit NoteLocks(const std::map<std::string, double>& params) {
        const auto& ids = VoiceParam::ids();
        for (const auto& [id, v] : params) {
            for (int k = 0; k < VoiceParam::count; ++k) {
                if (ids[size_t(k)] != id) continue;
                mask |= VoiceMask(1) << VoiceMask(k);
                vals[k] = float(v);
                break;
            }
        }
    }
};

}  // namespace jambo
