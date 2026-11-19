// Голоса сэмплера: создание, блок, голос (порт Core/Sampler/Sampler.swift).
#include "Sampler.h"
#include <algorithm>
#include <cmath>
#include "model/SynthEngines.h"
#include "util/RenderProfile.h"

namespace jambo {

Sampler::Sampler(double sampleRate) : sr(sampleRate) {
    const size_t s = slots;
    const size_t T = SoundData::maxTracks;
    const size_t mf = maxFrames;
    outL.assign(T * mf, 0.f);
    outR.assign(T * mf, 0.f);
    eqJobs.assign(T, 0);
    eqDone.assign(T, 0);
    active.assign(s, 0);
    fading.assign(s, 0);
    own.assign(s, 0);
    track.assign(s, 0);
    trackId.assign(s, 0);
    note.assign(s, 0);
    liveId.assign(s, -1);
    serial.assign(s, 0);
    srcL.assign(s, nullptr);
    srcR.assign(s, nullptr);
    srcFrames.assign(s, 0);
    stereo.assign(s, 0);
    start.assign(s, 0);
    pos.assign(s, 0);
    endPos.assign(s, 0);
    beginPos.assign(s, 0);
    tstr.assign(s, 0);
    grainRatio.assign(s, 1);
    srcBpm.assign(s, 0);
    onsetAt.assign(s, nullptr);
    onsetCount.assign(s, 0);
    warpN.assign(s, 0);
    warpU.assign(s * warpCap, 0);
    warpP.assign(s * warpCap, 0);
    uPos.assign(s, 0);
    edge.assign(s, 1);
    noteRatio.assign(s, 1);
    rate.assign(s, 1);
    attackF.assign(s, 48);
    decaySec.assign(s, 0.5);
    decayK.assign(s, 0);
    holdEnd.assign(s, 0);
    released.assign(s, 0);
    env.assign(s, 0);
    stopAt.assign(s, 0);
    fadeAt.assign(s, forever);
    fadeK.assign(s, 1);
    fadeG.assign(s, 1);
    level.assign(s, 1);
    pan.assign(s, 0);
    send.assign(s * 3, 0);
    sendL.assign(T * 3 * mf, 0.f);
    sendR.assign(T * 3 * mf, 0.f);
    duckCur.assign(T, 0);
    flagCur.assign(3, -1);
    hz.assign(s, 20000);
    qDb.assign(s, 0.7);
    ftype.assign(s, -1);
    coef.assign(s, Biquad());
    z.assign(s * 8, 0);
    fdip.assign(s, 1);
    lockMask.assign(s, 0);
    lockVal.assign(s * VoiceParam::count, 0);
    busL.assign(T * mf, 0.f);
    busR.assign(T * mf, 0.f);
    used.assign(T, 0);
    volCur.assign(T, -1);
    gateCur.assign(T, -1);
    meterL.assign(mf, 0.f);
    meterR.assign(mf, 0.f);
    // Номера без движка — 0: такой звук не синт.
    synthHash.assign(size_t(SynthEngines::count()), 0);
    for (const auto& e : SynthEngines::all()) synthHash[size_t(e.id)] = SoundKey::hash(SynthKey::key(e.id));
}

int Sampler::activeCount() const {
    int n = 0;
    for (int v = 0; v < slots; ++v)
        if (active[size_t(v)] != 0) ++n;
    return n;
}

float Sampler::cubic(const float* p, int n, double x) {
    if (x < 0) return 0;
    const int i = int(x);
    const float f = float(x - double(i));
    const float ym1 = i >= 1 && i - 1 < n ? p[i - 1] : 0;
    const float y0 = i < n ? p[i] : 0;
    const float y1 = i + 1 < n ? p[i + 1] : 0;
    const float y2 = i + 2 < n ? p[i + 2] : 0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2 * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

void Sampler::render(int64_t blockStart, int frames, float* left, float* right) {
    const int n = std::min(frames, maxFrames);
    if (!sound_) return;
    const SoundData& d = *sound_;
    vocoder.block = n;
    std::fill(used.begin(), used.end(), 0);
    for (int v = 0; v < slots; ++v) {
        if (active[size_t(v)] == 0) continue;
        const int t = track[size_t(v)];
        if (t >= d.trackCount) {
            finish(v);
            continue;
        }
        if (used[size_t(t)] == 0) {
            std::fill(busL.begin() + t * maxFrames, busL.begin() + t * maxFrames + n, 0.f);
            std::fill(busR.begin() + t * maxFrames, busR.begin() + t * maxFrames + n, 0.f);
            clearSends(t, n);
            used[size_t(t)] = 1;
        }
        renderVoice(v, d, blockStart, n);
    }
    lap(RenderProfile::voices);
    if (synth)
        synth->render(d, blockStart, n, busL.data(), busR.data(), sendL.data(), sendR.data(), used.data(), maxFrames);
    lap(RenderProfile::synth);
    wakeEqs(d, n);
    lap(RenderProfile::eq);
    if (fx) fx->prepare(blockStart, n);
    lap(RenderProfile::fx);
    mix(d, n, left, right);
    lap(RenderProfile::mix);
    if (fx) fx->process(n, bpm, left, right);
    lap(RenderProfile::fx);
}

void Sampler::clearSends(int t, int n) {
    for (int k = 0; k < 3; ++k) {
        const long o = long((t * 3 + k) * maxFrames);
        std::fill(sendL.begin() + o, sendL.begin() + o + n, 0.f);
        std::fill(sendR.begin() + o, sendR.begin() + o + n, 0.f);
    }
}

void Sampler::renderVoice(int v, const SoundData& d, int64_t blockStart, int n) {
    const size_t V = size_t(v);
    const float* l = srcL[V];
    const float* r = srcR[V];
    if (!l || !r) {
        finish(v);
        return;
    }
    const int t = track[V];
    const VoiceMask mask = lockMask[V];
    const int base = v * VoiceParam::count;
    // Ручки дорожки меняют звучащую ноту сразу; p-lock шага держит своё.
    const ModEngine* modE = mod && mod->modulated(t) ? mod : nullptr;
    auto val = [&](int p) -> double {
        double x = double(d.param(t, p));
        if (mask & (VoiceMask(1) << VoiceMask(p))) x = double(lockVal[size_t(base + p)]);
        if (modE) x = modE->value(t, p, x, blockStart);
        return x;
    };
    // RATE: PTCH — множитель скорости со сменой высоты; TSTR — скорость времени, высота — внутри зерна.
    const bool ts = tstr[V] != 0;
    const double rateK = liveId[V] == warpLive ? 1 : val(VoiceParam::rate) / 100;
    const double pitchR = noteRatio[V] * std::pow(2.0, val(VoiceParam::pitch) / 12);
    const double tempoK = srcBpm[V] > 0 ? bpm / srcBpm[V] : 1;
    const double rateT = ts ? tempoK * rateK : pitchR * rateK;
    const float levelT = float(std::pow(val(VoiceParam::level) / 127, 1.5));
    const size_t sb = V * 3;
    const float send0T = float(val(VoiceParam::reverb) / 100), send1T = float(val(VoiceParam::delay) / 100);
    const float send2T = float(val(VoiceParam::drive) / 100);
    float* sends0L = sendL.data() + t * 3 * maxFrames;
    float* sends0R = sendR.data() + t * 3 * maxFrames;
    const int mf = maxFrames;
    const float panT = float(std::max(-1.0, std::min(1.0, val(VoiceParam::pan) / 64)));
    const Biquad::Knob fk =
        Biquad::knob(val(VoiceParam::filter), val(VoiceParam::reso), val(VoiceParam::ftype), false);
    const int sub = 32;
    const double ks = 1 - std::exp(-double(sub) / (0.01 * sr));
    const double kf = 1 - std::exp(-double(sub) / (0.015 * sr));
    const int frames = srcFrames[V];
    const bool st = stereo[V] != 0;
    const size_t zb = V * 8;
    float* zz = z.data();
    float* bl = busL.data();
    float* br = busR.data();
    int i = 0;
    while (i < n) {
        const int m = std::min(sub, n - i);
        // Сглаживание ручек по кусочкам (10 мс, фильтр — 15 мс).
        rate[V] += (rateT - rate[V]) * ks;
        grainRatio[V] += (pitchR - grainRatio[V]) * ks;
        level[V] += (levelT - level[V]) * float(ks);
        pan[V] += (panT - pan[V]) * float(ks);
        send[sb] += (send0T - send[sb]) * float(ks);
        send[sb + 1] += (send1T - send[sb + 1]) * float(ks);
        send[sb + 2] += (send2T - send[sb + 2]) * float(ks);
        const float sg0 = send[sb], sg1 = send[sb + 1], sg2 = send[sb + 2];
        const bool hasSend = sg0 > 1e-6f || sg1 > 1e-6f || sg2 > 1e-6f;
        const double h0 = hz[V];
        const double q0 = qDb[V];
        // Частота — плавно; скачок — выход на сухой сигнал за 20 мс (с HP — 80 мс), фильтр сразу на цель, обратно.
        const float g0 = fdip[V];
        const bool swap = Biquad::glide(ftype[V], hz[V], fk.type, fk.hz, kf);
        const float dg = float(double(m) / (Biquad::fadeTime * sr));
        const float dgOut = ftype[V] == 1 ? float(double(m) / (Biquad::fadeHpTime * sr)) : dg;
        bool retype = false;
        if (swap && g0 <= 0) {
            ftype[V] = fk.type;
            hz[V] = fk.hz;
            for (int k = 0; k < 8; ++k) zz[zb + size_t(k)] = 0;
            retype = true;
        }
        fdip[V] = swap && !retype ? std::max(0.f, g0 - dgOut) : std::min(1.f, g0 + dg);
        qDb[V] += (fk.q - qDb[V]) * kf;
        if (retype || std::abs(hz[V] - h0) > 1e-6 || std::abs(qDb[V] - q0) > 1e-6)
            coef[V].set(ftype[V], hz[V], qDb[V], sr);
        const Biquad c = coef[V];
        float fq = retype ? 0 : Biquad::mix(g0);
        const float dfq = (Biquad::mix(fdip[V]) - fq) / float(m);
        const float pn = pan[V];
        // Панорама: моно — cos/sin; стерео — перекрёстно в сторону панорамы.
        const float x = st ? (pn <= 0 ? pn + 1 : pn) : (pn + 1) / 2;
        const float gc = std::cos(x * float(M_PI) / 2);
        const float gs = std::sin(x * float(M_PI) / 2);
        double rt = rate[V];
        const int skip = int(std::max<int64_t>(0, std::min<int64_t>(int64_t(m), start[V] - (blockStart + int64_t(i)))));
        // WARP: указатель u идёт со скоростью rate, отсчёт сэмпла — по карте.
        if (ts && warpN[V] > 0 && m > skip) rt = warpChunk(v, rt, m - skip);
        // PTCH около остановки (|RATE| < 5 %) — тише до нуля.
        const float lv = ts ? level[V] : level[V] * float(std::min(1.0, std::abs(rt) * 20));
        const bool beat = tstr[V] == 2;
        if (ts) {
            if (beat)
                stretch.fill(v, l, r, frames, st, pos[V], rt, grainRatio[V], m, skip, beginPos[V], endPos[V]);
            else
                vocoder.fill(v, l, r, frames, st, pos[V], rt, grainRatio[V], m, skip, beginPos[V], endPos[V], onsetAt[V],
                             onsetCount[V], blockStart + int64_t(i));
        }
        const float* tsL = beat ? stretch.outL.data() : vocoder.outL.data();
        const float* tsR = beat ? stretch.outR.data() : vocoder.outR.data();
        for (int j = 0; j < m; ++j) {
            const int64_t frame = blockStart + int64_t(i + j);
            if (frame < start[V]) continue;
            const bool ended = rt >= 0 ? pos[V] >= endPos[V] || pos[V] >= double(frames) : pos[V] <= beginPos[V];
            if (frame >= stopAt[V] || ended) {
                finish(v);
                return;
            }
            // Огибающая: атака линейно, удержание, затем Decay как Release (экспонента, τ = Decay/4).
            float e;
            if (frame >= holdEnd[V]) {
                if (released[V] == 0) {
                    released[V] = 1;
                    const double tt = double(frame - start[V]);
                    env[V] = tt < attackF[V] ? float(tt / attackF[V]) : 1;
                }
                env[V] *= decayK[V];
                e = env[V];
            } else {
                const double tt = double(frame - start[V]);
                e = tt < attackF[V] ? float(tt / attackF[V]) : 1;
                env[V] = e;
            }
            if (frame >= fadeAt[V]) fadeG[V] *= fadeK[V];
            const double remain = rt > 1e-9 ? (endPos[V] - pos[V]) / rt
                                             : (rt < -1e-9 ? (pos[V] - beginPos[V]) / -rt : 1e12);
            const float eg = float(std::min(1.0, remain / edge[V]));
            const float g = e * eg * lv * fadeG[V];
            float a = (ts ? tsL[j] : cubic(l, frames, pos[V])) * g;
            float b = st ? (ts ? tsR[j] : cubic(r, frames, pos[V])) * g : 0;
            // Фильтр (прямая форма I).
            const float ya = c.b0 * a + c.b1 * zz[zb] + c.b2 * zz[zb + 1] - c.a1 * zz[zb + 2] - c.a2 * zz[zb + 3];
            zz[zb + 1] = zz[zb];
            zz[zb] = a;
            zz[zb + 3] = zz[zb + 2];
            zz[zb + 2] = ya;
            fq += dfq;
            a += (ya - a) * fq;
            if (st) {
                const float yb = c.b0 * b + c.b1 * zz[zb + 4] + c.b2 * zz[zb + 5] - c.a1 * zz[zb + 6] - c.a2 * zz[zb + 7];
                zz[zb + 5] = zz[zb + 4];
                zz[zb + 4] = b;
                zz[zb + 7] = zz[zb + 6];
                zz[zb + 6] = yb;
                b += (yb - b) * fq;
            }
            const int k = t * maxFrames + i + j;
            float oa, ob;
            if (st) {
                if (pn <= 0) {
                    oa = a + b * gc;
                    ob = b * gs;
                } else {
                    oa = a * gc;
                    ob = b + a * gs;
                }
            } else {
                oa = a * gc;
                ob = a * gs;
            }
            bl[k] += oa;
            br[k] += ob;
            // Посылы голоса — после Pan.
            if (hasSend) {
                const int q = i + j;
                sends0L[q] += oa * sg0;
                sends0R[q] += ob * sg0;
                sends0L[mf + q] += oa * sg1;
                sends0R[mf + q] += ob * sg1;
                sends0L[2 * mf + q] += oa * sg2;
                sends0R[2 * mf + q] += ob * sg2;
            }
            pos[V] += rt;
        }
        // Денормалы в состоянии фильтра.
        for (int k = 0; k < 8; ++k)
            if (std::abs(zz[zb + size_t(k)]) < 1e-20f) zz[zb + size_t(k)] = 0;
        i += m;
    }
}

}  // namespace jambo
