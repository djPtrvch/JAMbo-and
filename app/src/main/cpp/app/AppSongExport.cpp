// Снимок проекта и транспорт для EXPORT SONG.
#include "App.h"

namespace jambo {

std::optional<SongRenderSource> App::beginSongExport() {
    if (songExport_ || project_.arrangeEnd() <= 0) return std::nullopt;
    if (playing_) togglePlay();
    const auto assets = projectAssets(project_);
    SongRenderSource input{project_, soundOptions(assets), {}};
    for (const auto& key : assets.samples)
        if (auto data = sampleData(key)) input.samples.emplace(key, std::move(data));
    songExport_ = true;
    audio_->setSongExportMute(true);
    bump();
    return input;
}

void App::endSongExport() {
    songExport_ = false;
    audio_->setSongExportMute(false);
    bump();
}

}  // namespace jambo
