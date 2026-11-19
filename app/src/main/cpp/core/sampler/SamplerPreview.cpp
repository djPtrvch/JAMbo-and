// Предпрослушка (порт Core/Sampler/SamplerPreview.swift): сэмпл в исходной высоте сразу на выход, мимо голосов и
// микшера; ▶ пресета — нота звуком пресета.
#include <algorithm>
#include <cmath>
#include "Sampler.h"

namespace jambo {

void Sampler::preview(uint64_t hash, double a, double b, int64_t at) {
    if (!sound_) return;
    const SoundData& d = *sound_;
    const int e = d.anyEntry(hash);
    if (e < 0) return;
    const double f = double(d.eFrames[size_t(e)]);
    pvL = d.eLeft[size_t(e)];
    pvR = d.eRight[size_t(e)];
    pvStereo = d.eChannels[size_t(e)] == 2;
    pvPos = std::max(0.0, std::min(1.0, a)) * f;
    pvEnd = std::max(0.0, std::min(1.0, b)) * f;
    pvGain = previewGain;
    pvStop = false;
    out.push(VoiceOut::start(previewVoice, -1, hash, at, pvPos / f, pvEnd / f, 1, d.eFrames[size_t(e)]));
}

void Sampler::stopPreview(int64_t at) {
    if (pvL) pvStop = true;
    for (int v = 0; v < slots; ++v)
        if (active[size_t(v)] != 0 && liveId[size_t(v)] == warpLive) fade(v, at, 0.005);
}

void Sampler::renderPreview(int n, float* left, float* right) {
    if (!pvL || !pvR) return;
    const float k = float(std::exp(-1 / (0.005 * sr)));
    for (int i = 0; i < n; ++i) {
        if (pvPos >= pvEnd || pvGain < 0.0005f) {
            pvL = nullptr;
            pvR = nullptr;
            out.push(VoiceOut::end(previewVoice));
            return;
        }
        const int j = int(pvPos);
        const float remain = float((pvEnd - pvPos) / (0.004 * sr));
        const float g = pvGain * std::min(1.f, remain);
        const float xl = pvL[j];
        const float xr = pvStereo ? pvR[j] : xl;
        left[i] += xl * g;
        right[i] += xr * g;
        pvPos += 1;
        if (pvStop) pvGain *= k;
    }
}

void Sampler::presetNote(int t, int n, uint64_t hash, const NoteLocks& locks, int64_t at) {
    for (int k = 0; k < VoiceParam::count; ++k) scratch[k] = locks.vals[k];
    one[0] = int16_t(n);
    const int64_t gate = at + int64_t(0.5 * sr);
    triggerNotes(t, at, one, 1, locks.mask, scratch, hash, gate, true, -1, false);
}

}  // namespace jambo
