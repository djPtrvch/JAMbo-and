// MIDI дорожки: вход нот и выход (порт структуры TrackMidi, Core/Midi/TrackMidi.swift, ветка swift JAMbo).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "../model/Eqs.h"

namespace jambo {

struct TrackMidi {
    /// Вход: принимать ли ноты (по умолчанию нет), устройство (nullopt — ANY), канал (0 — ALL, 1–16).
    bool inOn = false;
    std::optional<int32_t> inDevice;
    std::string inName;
    int inChannel = 0;
    /// Выход: устройство (nullopt — NONE) и канал 1–16.
    std::optional<int32_t> outDevice;
    std::string outName;
    int outChannel = 1;

    /// Ноты уходят на устройство (свой звук молчит).
    bool sends() const { return outDevice.has_value(); }
    /// Принимает ли дорожка ноту канала channel (0–15) с источника source; selected — дорожка выбрана.
    bool accepts(int32_t source, int channel, bool selected) const {
        if (!inOn) return false;
        if (inDevice && *inDevice != source) return false;
        if (inChannel == 0) return selected;
        return channel == inChannel - 1;
    }
    auto tie() const { return std::tie(inOn, inDevice, inName, inChannel, outDevice, outName, outChannel); }
    JAMBO_EQ(TrackMidi)
};

}  // namespace jambo
