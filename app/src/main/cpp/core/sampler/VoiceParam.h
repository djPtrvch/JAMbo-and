// Параметры голоса в снимках: номер в массиве (p-lock шагов — по тем же номерам). Порт VoiceParam,
// Core/Sampler/SoundSnapshot.swift (ветка swift JAMbo).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "model/AnalogMod.h"
#include "model/Params.h"

namespace jambo {

/// Маска p-lock голоса: бит на VoiceParam.
using VoiceMask = uint32_t;

namespace VoiceParam {
inline const std::vector<std::string>& ids() {
    static const std::vector<std::string> v = {"pitch", "start", "end", "level", "attack", "decay", "filter",
                                               "reso", "pan", "harm", "timbre", "morph", "reverb", "delay", "drive",
                                               "p4", "rate", AnalogMod::modId, AnalogMod::octId,
                                               Params::filterTypeId};
    return v;
}
constexpr int count = 20;
constexpr int pitch = 0, start = 1, end = 2, level = 3, attack = 4, decay = 5, filter = 6, reso = 7, pan = 8;
/// Синт: ручки 1.1–1.3 (0…100).
constexpr int harm = 9, timbre = 10, morph = 11;
/// Посылы Reverb, Delay, Drive (0…100).
constexpr int reverb = 12, delay = 13, drive = 14;
/// 1.4 синта.
constexpr int p4 = 15;
/// RATE сэмпла (−100…100 %).
constexpr int rate = 16;
/// ANALOG: режим модуляции и октава B; скрытые, без LFO/ENV.
constexpr int amod = 17, aoct = 18;
/// Тип фильтра; скрытый, без LFO/ENV.
constexpr int ftype = 19;
}  // namespace VoiceParam

}  // namespace jambo
