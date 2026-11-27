#pragma once

#include <functional>
#include "model/Project.h"
#include "sampler/SoundSnapshot.h"

namespace jambo {

/// Независимый снимок для фонового рендера. Звук и проект могут продолжать меняться в интерфейсе.
struct SongRenderSource {
    Project project;
    SoundSnapshot::Options options;
    std::map<std::string, SampleRef> samples;
};

/// Рендер общего микса в планарный PCM 48 кГц. tail < 0 — до тишины, максимум 10 секунд.
bool renderSong(SongRenderSource source, int tail,
                const std::function<bool(const float*, const float*, int)>& sink,
                const std::function<bool()>& cancelled,
                const std::function<void(double)>& progress);

}  // namespace jambo
