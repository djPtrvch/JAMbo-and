// Арпеджиатор: настройки дорожки и пределы (порт TrackArp и констант Arp, Core/Model/Arp.swift, ветка swift JAMbo;
// ряд нот — с секвенсором).
#pragma once
#include <algorithm>
#include "Eqs.h"

namespace jambo {

namespace Arp {
constexpr int dirCount = 6, rateCount = 5, maxOctaves = 3;
/// Направления (Arp.dirs) и шаги RATE в шестнадцатых (Arp.rateSteps).
constexpr int up = 0, down = 1, upDown = 2, downUp = 3, random = 4, order = 5;
constexpr double rateSteps[rateCount] = {2, 1.3333333333333333, 1, 0.6666666666666666, 0.5};
constexpr double gateMin = 0.1;
inline int clampDir(int d) { return std::max(0, std::min(dirCount - 1, d)); }
inline int clampRate(int r) { return std::max(0, std::min(rateCount - 1, r)); }
inline int clampOctaves(int o) { return std::max(1, std::min(maxOctaves, o)); }
inline double clampGate(double g) { return std::max(gateMin, std::min(1.0, g)); }
}  // namespace Arp

/// mode — режим кнопки CHORD: false — CHORD, true — ARP.
struct TrackArp {
    bool mode = false;
    bool on = false;
    int dir = 0;
    int rate = 2;
    int octaves = 1;
    double gate = 0.5;
    bool latch = false;

    /// Арпеджио играет: режим ARP и включён.
    bool active() const { return mode && on; }
    auto tie() const { return std::tie(mode, on, dir, rate, octaves, gate, latch); }
    JAMBO_EQ(TrackArp)
};

}  // namespace jambo
