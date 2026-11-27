#pragma once

#include <functional>
#include <string>

namespace jambo {

enum class SongCodec { wav, aac256, aac320, alac };

struct SongFormat {
    SongCodec codec;
    int depth;
    int rate;
};

using SongPcmSink = std::function<bool(const float*, const float*, int)>;

/// Потоковое сохранение планарного микса 48 кГц. writer производит PCM, sink передаётся в рендерер.
bool encodeSong(const std::string& path, SongFormat format,
                const std::function<bool(const SongPcmSink&)>& writer);

}  // namespace jambo
