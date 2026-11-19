// Запуск и остановка голосов сэмплера (порт Core/Sampler/SamplerNotes.swift): ноты дорожки, живые ноты, новый снимок
// звука. Всё — в колбэке, без выделений памяти.
#include <algorithm>
#include <cmath>
#include "Sampler.h"

namespace jambo {

double Sampler::pv(const SoundData& d, int t, int p, VoiceMask mask, const float* vals) const {
    double x = double(d.param(t, p));
    if ((mask & (VoiceMask(1) << VoiceMask(p))) && vals) x = double(vals[p]);
    // LFO/ENV — сверху (ENV — на кадр ноты).
    if (mod && mod->modulated(t)) x = mod->value(t, p, x, modFrame);
    return x;
}

void Sampler::triggerNotes(int t, int64_t frame, const int16_t* notes, int count, VoiceMask mask, const float* vals,
                           uint64_t hash, int64_t gate, bool isOwn, int live, bool retrig) {
    if (!sound_ || t < 0 || t >= sound_->trackCount) return;
    const SoundData& d = *sound_;
    modFrame = frame;
    // Sidechain: каждая нота источника (не перезапуск звука на держащейся ноте).
    if (!retrig && fx) fx->noteTrig(t, frame);
    const uint64_t h = hash != 0 ? hash : d.sound[size_t(d.s(t))];
    const int se = synthEngine(h);
    if (se >= 0) {
        if (synth) synth->trigger(d, t, se, frame, notes, count, mask, vals, gate, isOwn, live, retrig);
        return;
    }
    const int e = d.entry(t, h);
    if (e < 0) return;
    const int frames = d.eFrames[size_t(e)];
    const double start0 = warpPv ? 0 : double(frames) * pv(d, t, VoiceParam::start, mask, vals) / 100;
    const double end0 = warpPv ? double(frames) : double(frames) * pv(d, t, VoiceParam::end, mask, vals) / 100;
    // ▶ WARP: весь сэмпл в исходной высоте (без нарезки и Start/End).
    const int sc = warpPv ? 0 : int(d.eSliceCount[size_t(e)]);
    const double minLen = 0.003 * sr;
    if (sc == 0 && end0 - start0 < minLen) return;
    const double attack = pv(d, t, VoiceParam::attack, mask, vals) / 1000;
    const double decay = pv(d, t, VoiceParam::decay, mask, vals) / 1000;
    if (!retrig) legatoCut(t, notes, count, frame, attack);
    for (int k = 0; k < count; ++k) {
        const int n = int(notes[k]);
        double a = start0, b = end0;
        double ratio = std::pow(2.0, double(n - Params::sampleRoot) / 12);
        if (sc > 0) {
            const int s0 = int(d.eSliceStart[size_t(e)]);
            int idx = -1;
            for (int j = 0; j < sc; ++j) {
                if (int(d.sliceNote[size_t(s0 + j)]) == n) {
                    idx = j;
                    break;
                }
            }
            if (idx < 0) continue;
            a = d.sliceAt[size_t(s0 + idx)] * double(frames);
            b = (idx + 1 < sc ? d.sliceAt[size_t(s0 + idx + 1)] : 1) * double(frames);
            if (b - a < minLen) continue;
            ratio = 1;
        }
        const int v = allocate(frame);
        setupVoice(v, d, t, e, frame, a, b, ratio, n, attack, decay, gate, mask, vals);
        own[size_t(v)] = isOwn ? 1 : 0;
        liveId[size_t(v)] = live;
        // Полоса воспроизведения: реверс — от конца отрезка к началу.
        const double fa = a / double(frames), fb = b / double(frames);
        const bool rev = rate[size_t(v)] < 0;
        out.push(VoiceOut::start(v, t, h, frame, rev ? fb : fa, rev ? fa : fb, rate[size_t(v)], frames));
    }
}

void Sampler::setupVoice(int v, const SoundData& d, int t, int e, int64_t frame, double a, double b, double ratio,
                         int n, double attack, double decay, int64_t gate, VoiceMask mask, const float* vals) {
    const size_t V = size_t(v);
    const size_t E = size_t(e);
    active[V] = 1;
    fading[V] = 0;
    track[V] = t;
    trackId[V] = d.trackId[size_t(t)];
    note[V] = n;
    srcL[V] = d.eLeft[E];
    srcR[V] = d.eRight[E];
    srcFrames[V] = d.eFrames[E];
    stereo[V] = d.eChannels[E] == 2 ? 1 : 0;
    start[V] = frame;
    beginPos[V] = a;
    endPos[V] = b;
    noteRatio[V] = ratio;
    lockMask[V] = mask;
    for (int p = 0; p < VoiceParam::count; ++p) lockVal[V * VoiceParam::count + size_t(p)] = vals ? vals[p] : 0;
    const double pitch = pv(d, t, VoiceParam::pitch, mask, vals);
    const double pitchR = ratio * std::pow(2.0, pitch / 12);
    const double rateK = warpPv ? 1 : pv(d, t, VoiceParam::rate, mask, vals) / 100;
    // RATE: PTCH — скорость и высота вместе; TSTR — скорость времени по темпу проекта.
    const uint8_t mode = warpPv ? d.stretchAlgo : d.rateMode[size_t(d.s(t))];
    const bool ts = mode != 0;
    tstr[V] = mode;
    srcBpm[V] = ts ? d.eBpm[E] : 0;
    onsetAt[V] = mode == 1 ? d.eOnsets[E] : nullptr;
    onsetCount[V] = mode == 1 ? d.eOnsetCount[E] : 0;
    grainRatio[V] = pitchR;
    const double tempoK = srcBpm[V] > 0 ? bpm / srcBpm[V] : 1;
    rate[V] = ts ? tempoK * rateK : pitchR * rateK;
    // Реверс — от конца отрезка.
    pos[V] = rate[V] < 0 ? b - 1 : a;
    setupWarp(v, d, e, ts);
    if (mode == 2) stretch.start(v, rate[V]);
    if (mode == 1) vocoder.start(v, rate[V]);
    // Край сэмпла (End или конец файла) гаснет за 4 мс (не больше четверти звучания).
    const double playDur = (b - a) / std::max(1e-3, std::abs(rate[V]));
    edge[V] = std::max(1.0, std::min(0.004 * sr, playDur / 4));
    const double att = std::max(0.001, attack) * sr;
    attackF[V] = att;
    decaySec[V] = decay;
    decayK[V] = float(std::exp(-1 / (std::max(0.005, decay / 4) * sr)));
    if (gate == forever) {
        holdEnd[V] = forever;
        stopAt[V] = forever;
    } else {
        const double hold = gate < 0 ? att : std::max(att, double(gate));
        holdEnd[V] = frame + int64_t(std::round(hold));
        stopAt[V] = holdEnd[V] + int64_t((std::max(0.02, decay) * 2 + 0.05) * sr);
    }
    released[V] = 0;
    env[V] = 0;
    fadeAt[V] = forever;
    fadeG[V] = 1;
    fadeK[V] = 1;
    level[V] = float(std::pow(pv(d, t, VoiceParam::level, mask, vals) / 127, 1.5));
    pan[V] = float(std::max(-1.0, std::min(1.0, pv(d, t, VoiceParam::pan, mask, vals) / 64)));
    for (int k = 0; k < 3; ++k) send[V * 3 + size_t(k)] = float(pv(d, t, VoiceParam::reverb + k, mask, vals) / 100);
    const Biquad::Knob f = Biquad::knob(pv(d, t, VoiceParam::filter, mask, vals), pv(d, t, VoiceParam::reso, mask, vals),
                                        pv(d, t, VoiceParam::ftype, mask, vals), false);
    ftype[V] = f.type;
    hz[V] = f.hz;
    qDb[V] = f.q;
    coef[V].set(f.type, f.hz, f.q, sr);
    fdip[V] = 1;
    for (size_t k = 0; k < 8; ++k) z[V * 8 + k] = 0;
}

int Sampler::allocate(int64_t frame) {
    int free = -1, live = 0, oldest = -1, oldestAny = -1;
    for (int v = 0; v < slots; ++v) {
        const size_t V = size_t(v);
        if (active[V] == 0) {
            if (free < 0) free = v;
            continue;
        }
        if (oldestAny < 0 || serial[V] < serial[size_t(oldestAny)]) oldestAny = v;
        if (fading[V] != 0) continue;
        ++live;
        if (oldest < 0 || serial[V] < serial[size_t(oldest)]) oldest = v;
    }
    if (live >= maxVoices && oldest >= 0) {
        fade(oldest, frame, 0.0015);
        stopAt[size_t(oldest)] = std::min(stopAt[size_t(oldest)], frame + int64_t(0.02 * sr));
    }
    if (free < 0) {
        // Все ячейки заняты гаснущими — самая старая освобождается сразу.
        free = std::max(0, oldestAny);
        finish(free);
    }
    ++nextSerial;
    serial[size_t(free)] = nextSerial;
    return free;
}

void Sampler::fade(int v, int64_t at, double tau) {
    const size_t V = size_t(v);
    if (fading[V] != 0 && fadeAt[V] <= at) return;
    fading[V] = 1;
    fadeAt[V] = at;
    fadeK[V] = float(std::exp(-1 / (tau * sr)));
    stopAt[V] = std::min(stopAt[V], at + int64_t((tau * 8 + 0.005) * sr));
}

void Sampler::finish(int v) {
    if (active[size_t(v)] == 0) return;
    active[size_t(v)] = 0;
    out.push(VoiceOut::end(v));
    // ▶ WARP закончился — кнопка ▶ снова ▶.
    if (liveId[size_t(v)] == warpLive) out.push(VoiceOut::end(previewVoice));
}

void Sampler::legatoCut(int t, const int16_t* notes, int count, int64_t at, double attack) {
    const int64_t tol = int64_t(0.005 * sr);
    for (int v = 0; v < slots; ++v) {
        const size_t V = size_t(v);
        if (!(active[V] != 0 && fading[V] == 0 && track[V] == t && holdEnd[V] != forever)) continue;
        bool same = false;
        for (int k = 0; k < count; ++k)
            if (int(notes[k]) == note[V]) same = true;
        if (!same) continue;
        if (start[V] < at && holdEnd[V] >= at - tol) fade(v, at, std::max(0.0015, attack / 3));
    }
}

void Sampler::release(int t, int id, int64_t at) {
    midiRelease(id, at);
    if (synth) synth->release(t, id, at);
    for (int v = 0; v < slots; ++v) {
        const size_t V = size_t(v);
        if (!(active[V] != 0 && track[V] == t && liveId[V] == id && holdEnd[V] == forever)) continue;
        holdEnd[V] = std::max(at, start[V]);
        stopAt[V] = holdEnd[V] + int64_t((std::max(0.02, decaySec[V]) * 2 + 0.05) * sr);
    }
}

void Sampler::noteOn(int t, int n, int id, float velocity, int64_t at) {
    if (!sound_ || t >= sound_->trackCount) return;
    const SoundData& d = *sound_;
    one[0] = int16_t(n);
    VoiceMask mask = 0;
    if (velocity < 1) {
        mask = VoiceMask(1) << VoiceMask(VoiceParam::level);
        scratch[VoiceParam::level] = d.param(t, VoiceParam::level) * velocity;
    }
    if (sendsMidi(t)) {
        sidechainTrig(t, at);
        midiNotes(t, at, one, 1, mask, scratch, -1, id);
        return;
    }
    triggerNotes(t, at, one, 1, mask, scratch, 0, forever, false, id, false);
}

void Sampler::adopt(SoundData& d, int64_t now) {
    const SoundData* old = sound_;
    sound_ = &d;
    for (int v = 0; v < slots; ++v) {
        const size_t V = size_t(v);
        if (active[V] == 0) continue;
        const int t = track[V];
        if (d.rebind && t < d.trackCount) {
            trackId[V] = d.trackId[size_t(t)];
        } else if (t >= d.trackCount || d.trackId[size_t(t)] != trackId[V]) {
            const int t2 = indexOf(trackId[V], d);
            if (t2 < 0) {
                fade(v, now, 0.0015);
                continue;
            }
            track[V] = t2;
        }
        if (srcL[V] && !d.holds(srcL[V])) fade(v, now, 0.0015);
    }
    if (old && !d.rebind) moveSynthNotes(*old, d, now);
    // Предпрослушка ▶ держит указатели прошлого снимка: сэмпла в новом нет — гаснет.
    if (pvL && !d.holds(pvL)) pvStop = true;
    if (synth) synth->adopt(d, now);
    if (!old) return;
    const SoundData& o = *old;
    const int n = std::min(d.trackCount, o.trackCount);
    const int lastSynth = synth ? synth->lastSerial() : 0;
    for (int t = 0; t < n; ++t) {
        if (d.trackId[size_t(t)] == o.trackId[size_t(t)] && d.sound[size_t(d.s(t))] != o.sound[size_t(o.s(t))]) {
            retrigger(t, now);
            retriggerSynth(t, now, lastSynth);
        }
    }
}

int Sampler::indexOf(int id, const SoundData& d) {
    for (int t = 0; t < d.trackCount; ++t)
        if (d.trackId[size_t(t)] == id) return t;
    return -1;
}

void Sampler::moveSynthNotes(const SoundData& o, const SoundData& d, int64_t now) {
    if (!synth) return;
    const int last = synth->lastSerial();
    for (int t = 0; t < o.trackCount; ++t) {
        const int id = o.trackId[size_t(t)];
        const bool same = t < d.trackCount && d.trackId[size_t(t)] == id;
        const int t2 = same ? t : indexOf(id, d);
        if (same || t2 < 0) continue;
        synth->retrigger(t, now, last,
                         [&](int n, int64_t at, int64_t gate, VoiceMask mask, const float* vals, int live) {
                             one[0] = int16_t(n);
                             for (int p = 0; p < VoiceParam::count; ++p) scratch[p] = vals[p];
                             triggerNotes(t2, at, one, 1, mask, scratch, 0, gate, false, live, true);
                         });
    }
}

void Sampler::retrigger(int t, int64_t now) {
    const int64_t edgeTol = int64_t(0.01 * sr);
    // Новые голоса (созданные в этом же цикле) не перезапускаются.
    const int last = nextSerial;
    for (int v = 0; v < slots; ++v) {
        const size_t V = size_t(v);
        if (!(active[V] != 0 && fading[V] == 0 && track[V] == t && own[V] == 0 && serial[V] <= last)) continue;
        const int64_t at = std::max(now, start[V]);
        if (holdEnd[V] != forever && holdEnd[V] <= at + edgeTol) continue;
        const int64_t gate = holdEnd[V] == forever ? forever : holdEnd[V] - at;
        fade(v, at, 0.003);
        one[0] = int16_t(note[V]);
        const size_t base = V * VoiceParam::count;
        for (size_t p = 0; p < size_t(VoiceParam::count); ++p) scratch[p] = lockVal[base + p];
        triggerNotes(t, at, one, 1, lockMask[V], scratch, 0, gate, false, liveId[V], true);
    }
}

}  // namespace jambo
