// Свои значения ручек по умолчанию: ключи и спутники (порт статических частей Core/Model/KnobDefaults.swift, ветка
// swift JAMbo). Значения, EQ, apply и JSON — с моделью проекта (Track, Project, EqBand).
#pragma once
#include <string>
#include "Params.h"
#include "SynthEngines.h"

namespace jambo {
namespace KnobDefaults {

/// Область ручек звука дорожки: synthN или sampler.
inline std::string soundScope(const std::string& sound) {
    const int e = SynthKey::engine(sound);
    return e >= 0 ? "synth" + std::to_string(e) : "sampler";
}

/// Ключ «область/id».
inline std::string key(const std::string& scope, const std::string& id) { return scope + "/" + id; }

/// Что запоминается вместе со значением ручки (от чего зависит его смысл); пусто — ничего.
inline std::string companion(const std::string& id) {
    if (id == "dly.time") return "dly.sync";
    if (id == "rate") return Params::rateModeId;
    if (id == "filter") return Params::filterTypeId;
    for (size_t l = 0; l < Params::lfoSyncIds.size(); ++l)
        if (id == "lfo" + std::to_string(l + 1) + ".speed") return Params::lfoSyncIds[l];
    return "";
}

}  // namespace KnobDefaults
}  // namespace jambo
