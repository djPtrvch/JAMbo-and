// Звук дорожки без паттернов: переезжает с клипом PREV перехода MIX (порт TrackSound, Core/Model/Transition.swift,
// ветка swift JAMbo; переход — позже).
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "ModAssign.h"
#include "Params.h"
#include "Sounds.h"
#include "Warp.h"
#include "../eq/EqModel.h"

namespace jambo {

struct TrackSound {
    std::string sample;
    ParamMap params;
    Folder folder;
    std::map<std::string, std::vector<SliceMark>> slices;
    std::optional<std::map<std::string, WarpInfo>> warps;
    std::string table;
    std::optional<std::string> grain;
    std::map<std::string, ModAssign> assigns;
    Eq eq;
    /// Громкость микшера (dB) клипа со своим звуком; nullopt — громкость дорожки.
    std::optional<double> vol;

    auto tie() const { return std::tie(sample, params, folder, slices, warps, table, grain, assigns, eq, vol); }
    JAMBO_EQ(TrackSound)
};

}  // namespace jambo
