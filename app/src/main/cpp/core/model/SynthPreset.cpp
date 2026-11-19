// Порт Core/Model/SynthPreset.swift (ветка swift JAMbo): значения пресета.
#include "SynthPreset.h"
#include "AnalogMod.h"
#include "FilterType.h"
#include "SynthEngines.h"
#include "../synth/Jambo.h"

namespace jambo {

const std::vector<std::string>& SynthPreset::paramIds() {
    static const std::vector<std::string> v = {"harm", "timbre", "morph", "p4", "level", "attack", "decay", "filter",
                                               "reso", Params::filterTypeId, AnalogMod::modId, AnalogMod::octId};
    return v;
}

std::optional<double> SynthPreset::value(const std::string& id) const {
    const auto it = params.find(id);
    if (it != params.end()) return it->second;
    // Тип фильтра: нет в пресете с ручкой Filter — LP.
    if (id == Params::filterTypeId && params.count("filter")) return double(FilterType::lp);
    if ((id == AnalogMod::modId || id == AnalogMod::octId) && SynthKey::engine(sound) == J::analog) return 0.0;
    return std::nullopt;
}

}  // namespace jambo
