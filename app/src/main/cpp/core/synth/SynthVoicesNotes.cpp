// Порт Core/Synth/SynthVoicesEnvelope.swift и SynthVoicesNotes.swift (ветка swift JAMbo): огибающая куска, выбор
// слота, legato, отпускание живых нот, новый снимок звука.
#include <cfloat>
#include "SynthVoices.h"
#include "TrackRotate.h"

namespace jambo {

bool SynthVoices::envelope(int v, int64_t frame0, int m, float lv) {
    const size_t i = size_t(v);
    const int64_t stop = stopAt[i], hold = holdEnd[i], fadeFrom = fadeAt[i];
    const double att = attackF[i];
    const bool keepEnv = keep[i] != 0;
    const bool decaying = selfDecay[i] != 0;
    const float e0v = env0[i];
    const float dk = decayK[i], fadeKV = fadeK[i];
    float envV = env[i], fadeV = fadeG[i];
    bool rel = released[i] != 0;
    float* gb = gBuf.data() + (v / perTrack) * chunk;
    int64_t frame = frame0;
    double tt = double(frame0 - start[i]);
    int j = 0;
    bool stopped = false;
    while (j < m) {
        if (frame >= stop) {
            stopped = true;
            break;
        }
        float g;
        if (keepEnv) {
            g = 1;
        } else if (decaying) {
            g = tt < att ? float(tt / att) : 1;
        } else if (frame >= hold) {
            if (!rel) {
                rel = true;
                envV = tt < att ? e0v + (1 - e0v) * float(tt / att) : 1;
            }
            envV *= dk;
            g = envV;
        } else {
            g = tt < att ? e0v + (1 - e0v) * float(tt / att) : 1;
            envV = g;
        }
        if (frame >= fadeFrom) fadeV *= fadeKV;
        gb[j] = g * lv * fadeV;
        frame += 1;
        tt += 1;
        j += 1;
    }
    for (; j < m; ++j) gb[j] = 0;
    env[i] = envV;
    fadeG[i] = fadeV;
    released[i] = rel ? 1 : 0;
    return stopped;
}

void SynthVoices::release(int t, int id, int64_t at) {
    if (t < 0 || t >= tracks) return;
    const int base = t * perTrack;
    for (int v = base; v < base + perTrack; ++v) {
        const size_t i = size_t(v);
        if (active[i] != 0 && liveId[i] == id && holdEnd[i] == forever) {
            holdEnd[i] = std::max(at, start[i]);
            stopAt[i] = holdEnd[i] + int64_t((std::max(0.02, decaySec[i]) * 2 + 0.05) * sr);
        }
        if (pActive[i] != 0 && pLive[i] == id && pGate[i] == forever) pGate[i] = at > pStart[i] ? at - pStart[i] : -1;
    }
}

bool SynthVoices::moveTracks(const SoundData& old, const SoundData& d) {
    if (old.trackCount != d.trackCount || d.rebind) return false;
    const TrackMove mv = TrackMove::find(old.trackId.data(), d.trackId.data(), d.trackCount);
    if (!mv.valid()) return false;
    const int T = tracks;
    mv.applyPerTrack(active, T); mv.applyPerTrack(fading, T); mv.applyPerTrack(own, T);
    mv.applyPerTrack(released, T); mv.applyPerTrack(triggered, T);
    mv.applyPerTrack(trackId, T); mv.applyPerTrack(note, T); mv.applyPerTrack(liveId, T);
    mv.applyPerTrack(serial, T); mv.applyPerTrack(engine, T);
    mv.applyPerTrack(start, T); mv.applyPerTrack(holdEnd, T); mv.applyPerTrack(stopAt, T); mv.applyPerTrack(fadeAt, T);
    mv.applyPerTrack(attackF, T); mv.applyPerTrack(decaySec, T);
    mv.applyPerTrack(decayK, T); mv.applyPerTrack(env, T); mv.applyPerTrack(env0, T); mv.applyPerTrack(fadeK, T);
    mv.applyPerTrack(fadeG, T); mv.applyPerTrack(level, T); mv.applyPerTrack(send, T); mv.applyPerTrack(pan, T);
    mv.applyPerTrack(hz, T); mv.applyPerTrack(qDb, T); mv.applyPerTrack(ftype, T); mv.applyPerTrack(coef, T);
    mv.applyPerTrack(z, T); mv.applyPerTrack(zR, T); mv.applyPerTrack(fdip, T);
    ladder.moveTrack(mv, T);
    mv.applyPerTrack(busyUntil, T); mv.applyPerTrack(selfDecay, T); mv.applyPerTrack(keep, T);
    mv.applyPerTrack(quiet, T); mv.applyPerTrack(lockMask, T); mv.applyPerTrack(lockVal, T);
    mv.applyPerTrack(pActive, T); mv.applyPerTrack(pStart, T); mv.applyPerTrack(pNote, T); mv.applyPerTrack(pEngine, T);
    mv.applyPerTrack(pGate, T); mv.applyPerTrack(pOwn, T); mv.applyPerTrack(pKeep, T); mv.applyPerTrack(pLive, T);
    mv.applyPerTrack(pMask, T); mv.applyPerTrack(pVal, T);
    mv.applyPerTrack(next, T); mv.applyPerTrack(drift, T);
    bank.moveTrack(mv);
    if (mod) mod->moveTrack(mv);
    return true;
}

void SynthVoices::adopt(const SoundData& d, int64_t now) {
    for (int v = 0; v < slots; ++v) {
        const size_t i = size_t(v);
        if (active[i] == 0 && pActive[i] == 0) continue;
        const int t = v / perTrack;
        if (d.rebind && t < d.trackCount) {
            trackId[i] = d.trackId[size_t(t)];
            continue;
        }
        if (t >= d.trackCount || d.trackId[size_t(t)] != trackId[i]) {
            pActive[i] = 0;
            fade(v, now, 0.0015);
        }
    }
}

SynthVoices::Slot SynthVoices::allocate(int t, int64_t at) {
    const int base = t * perTrack;
    const int pt = perTrack;
    int quietV = -1;
    float quietG = FLT_MAX;
    for (int k = 0; k < pt; ++k) {
        const int c = (next[size_t(t)] + k) % pt;
        const int v = base + c;
        const size_t i = size_t(v);
        if (pActive[i] != 0) continue;
        if (active[i] == 0 || std::min(stopAt[i], busyUntil[i]) <= at) {
            next[size_t(t)] = (c + 1) % pt;
            return {v, false};
        }
        // Держащаяся нота — громче любого хвоста.
        const float held = holdEnd[i] > at ? 1 : 0;
        const float g = held + env[i] * fadeG[i] * level[i];
        if (g < quietG) {
            quietG = g;
            quietV = v;
        }
    }
    if (quietV >= 0) return {quietV, true};
    const int c = next[size_t(t)] % pt;
    next[size_t(t)] = (c + 1) % pt;
    return {base + c, true};
}

void SynthVoices::legatoCut(int t, const int16_t* notes, int count, int64_t at, double attack) {
    const int64_t tol = int64_t(0.005 * sr);
    const int base = t * perTrack;
    for (int v = base; v < base + perTrack; ++v) {
        const size_t i = size_t(v);
        if (!(active[i] != 0 && fading[i] == 0 && holdEnd[i] != forever)) continue;
        bool same = false;
        for (int k = 0; k < count; ++k)
            if (int(notes[k]) == note[i]) same = true;
        if (!same) continue;
        if (start[i] < at && holdEnd[i] >= at - tol) fade(v, at, std::max(0.0015, attack / 3));
    }
}

}  // namespace jambo
