// Sampler ↔ голоса синта (порт Core/Synth/SamplerSynth.swift): звук «mi:<n>» по ключу и перезапуск держащихся нот синта
// новым звуком дорожки.
#include "Sampler.h"

namespace jambo {

int Sampler::synthEngine(uint64_t h) const {
    if (h == 0) return -1;
    for (size_t e = 0; e < synthHash.size(); ++e)
        if (synthHash[e] == h) return int(e);
    return -1;
}

void Sampler::retriggerSynth(int t, int64_t now, int last) {
    if (!synth) return;
    synth->retrigger(t, now, last, [&](int n, int64_t at, int64_t gate, VoiceMask mask, const float* vals, int live) {
        one[0] = int16_t(n);
        for (int p = 0; p < VoiceParam::count; ++p) scratch[p] = vals[p];
        triggerNotes(t, at, one, 1, mask, scratch, 0, gate, false, live, true);
    });
}

}  // namespace jambo
