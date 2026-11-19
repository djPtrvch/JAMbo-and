// Версия 4 (S12): ручки своих движков после появления 1.4 (порт SynthMigration.knobs, Core/Model/SynthMigration.swift).
#pragma once
#include "../synth/Jambo.h"

namespace jambo {

struct MigratedKnobs { double h, t, m, p4; bool lostSub; };

/// Ручки 1.1–1.4 (0…100) движка e после S12 по прежним 1.1–1.3; lostSub — у ANALOG был суб.
inline MigratedKnobs migrateKnobs(int e, double h, double t, double m) {
    switch (e) {
    case J::drum: return {h, t, m, m, false};               // CLICK — прежний уровень клика
    case J::analog: return {h, h, t, 50, m > 0};            // A и B — прежний WAVE, DETUNE с 1.2 на 1.3, MIX 50 %
    case J::wtable: {
        // UNISON до 25 % — один голос, выше — три; DETUNE — прежние центы.
        if (m <= 25) return {h, t, 100.0 / 6, 50, false};
        const double cents = J::wtUnisonOld * (m - 25) / 75;
        return {h, t, 50, cents / J::wtDetune * 100, false};
    }
    default: return {h, t, m, 50, false};
    }
}

}  // namespace jambo
