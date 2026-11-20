// Порт Core/Synth/JamboBank.swift (ветка swift JAMbo).
#include "JamboBank.h"
#include "TrackRotate.h"
#include <algorithm>
#include "Jambo.h"

namespace jambo {

JamboBank::JamboBank(int count, double sampleRate, int perTrack)
    : count(count), tracks((count + perTrack - 1) / perTrack), perTrack(perTrack), kind(size_t(count), 0) {
    for (int v = 0; v < count; ++v) {
        jambo.push_back(std::make_unique<JamboVoice>(jamboTables, sampleRate));
        drums.push_back(std::make_unique<JamboDrumVoice>(jamboTables, sampleRate));
    }
    grainTracks.reset(new GrainTrack[size_t(tracks)]);
    for (int t = 0; t < tracks; ++t) {
        texture.push_back(std::make_unique<TextureVoice>(sampleRate, t + 1));
        grainTracks[size_t(t)].rnd = int64_t((uint64_t((t + 1) * 40503 + 11) & 0xFFFFFFFFULL) | 1);
    }
    for (int v = 0; v < count; ++v)
        granular.push_back(std::make_unique<GranularVoice>(sampleRate, v + 1, &grainTracks[size_t(v / perTrack)]));
}

void JamboBank::moveTrack(const TrackMove& mv) {
    mv.applyPerTrack(jambo, tracks);
    mv.applyPerTrack(drums, tracks);
    mv.applyPerTrack(kind, tracks);
    mv.applyPerTrack(texture, tracks);
    mv.applyPerTrack(granular, tracks);
}

void JamboBank::jamboTrigger(int v, int e, double note, double p1, double p2, double p3, double p4, double decay,
                             double now, double tau) {
    if (e >= J::snare && e <= J::perc) {
        kind[size_t(v)] = 1;
        drums[size_t(v)]->trigger(e, p1, p2, p3, p4, decay);
        return;
    }
    if (e == J::texture) {
        kind[size_t(v)] = 2;
        texture[size_t(v / perTrack)]->trigger(note, p1, p2, p3, p4);
        return;
    }
    if (e == J::granular) {
        kind[size_t(v)] = 3;
        granular[size_t(v)]->trigger(note, p1, p2, p3, p4);
        return;
    }
    kind[size_t(v)] = 0;
    jambo[size_t(v)]->trigger(e, note, p1, p2, p3, p4, decay, now, tau);
}

void JamboBank::jamboSet(int v, double note, double p1, double p2, double p3, double p4, double decay) {
    const uint8_t k = kind[size_t(v)];
    if (k == 1) {
        drums[size_t(v)]->set(p1, p2, p3, p4, decay);
        return;
    }
    if (k == 2) {
        texture[size_t(v / perTrack)]->set(note, p1, p2, p3, p4);
        return;
    }
    if (k == 3) {
        granular[size_t(v)]->set(note, p1, p2, p3, p4);
        return;
    }
    jambo[size_t(v)]->set(note, p1, p2, p3, p4, decay);
}

void JamboBank::jamboRender(int v, float* out, int n, double driftRatio, const WtRef& table) {
    if (kind[size_t(v)] == 1) {
        drums[size_t(v)]->render(out, n);
        return;
    }
    jambo[size_t(v)]->render(out, n, driftRatio, table);
}

void JamboBank::jamboSilent(int v) {
    jambo[size_t(v)]->last = 0;
    drums[size_t(v)]->last = 0;
    granular[size_t(v)]->silence();
    if (v % perTrack == 0) texture[size_t(v / perTrack)]->silence();
}

void JamboBank::stereoRender(int v, float* outL, float* outR, int n, int window, const GrainSrc& src) {
    if (kind[size_t(v)] == 3) {
        granular[size_t(v)]->render(outL, outR, n, window, src);
        return;
    }
    texture[size_t(v / perTrack)]->render(outL, outR, n, window);
}

void JamboBank::granularBegin(int n, double sampleRate, const uint8_t* active) {
    const double dt = double(n) / sampleRate;
    for (int t = 0; t < tracks; ++t) {
        int c = 0;
        bool any = false;
        const int end = std::min(count, t * perTrack + perTrack);
        for (int v = t * perTrack; v < end; ++v) {
            if (kind[size_t(v)] == 3 && active[v] != 0) {
                any = true;
                if (granular[size_t(v)]->spawning()) c += 1;
            }
        }
        grainTracks[size_t(t)].notes = c;
        if (any) grainTracks[size_t(t)].advance(dt);
    }
}

}  // namespace jambo
