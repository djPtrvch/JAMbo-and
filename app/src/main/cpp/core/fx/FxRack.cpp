// Эффекты дорожек и Sidechain (порт Core/FX/FxRack.swift).
#include "FxRack.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace jambo {

namespace {
float peak(const float* l, const float* r, int n) {
    float m = 0;
    for (int i = 0; i < n; ++i) {
        const float a = l[i] < 0 ? -l[i] : l[i];
        const float b = r[i] < 0 ? -r[i] : r[i];
        if (a > m) m = a;
        if (b > m) m = b;
    }
    return m;
}
void add(int n, float g, const float* xl, const float* xr, float* ol, float* orr) {
    for (int i = 0; i < n; ++i) {
        ol[i] += xl[i] * g;
        orr[i] += xr[i] * g;
    }
}
}  // namespace

uint32_t FxRack::seed() {
    static std::random_device rd;
    std::uniform_int_distribution<uint32_t> u(1, UINT32_MAX);
    return u(rd);
}

FxRack::FxRack(double sampleRate, int mf) : duck(sampleRate), sr(sampleRate), maxFrames(mf) {
    const int nt = tracks;
    for (const auto& d : Params::delayDivs) delayBeats.push_back(d.beats);
    delayDivs = int(delayBeats.size());
    for (int t = 0; t < nt; ++t) {
        reverb.push_back(std::make_unique<FxReverb>(sampleRate, seed()));
        delay.push_back(std::make_unique<FxDelay>(sampleRate, maxDelay(), seed()));
        drive.push_back(std::make_unique<FxDrive>(sampleRate));
    }
    // Вся линия Delay переписана тишиной — только тогда сон.
    dlyIdleSec = double(delay[0]->len()) / sampleRate + 0.5;
    inL.assign(size_t(nt * 3 * mf), 0.f);
    inR.assign(size_t(nt * 3 * mf), 0.f);
    fed.assign(size_t(nt), 0);
    e.assign(size_t(mf), 0.f);
    outL.assign(size_t(nt * mf), 0.f);
    outR.assign(size_t(nt * mf), 0.f);
    tmpL.assign(size_t(nt * mf), 0.f);
    tmpR.assign(size_t(nt * mf), 0.f);
    for (const auto& id : Params::fxIds) {
        const ParamDef* p = Params::byId(id);
        live.push_back(p ? p->def : 0);
    }
    for (int t = 0; t < nt; ++t) tlive.insert(tlive.end(), live.begin(), live.end());
    amount.assign(size_t(nt), 0.f);
    sendFlag.assign(3, -1.f);
    revQuiet.assign(size_t(nt), 0);
    dlyQuiet.assign(size_t(nt), 0);
    odQuiet.assign(size_t(nt), 0);
    // Покой: дорожка·3 + эффект; с начала — спят, первый посыл будит.
    idle.assign(size_t(nt * 3), 1);
    jobTracks.assign(size_t(nt), 0);
}

void FxRack::begin(const SoundData& d, const uint32_t* lockMask, const float* lockVal, const ModEngine* mod,
                   int64_t frame) {
    const int c = Params::fxCount;
    for (int p = 0; p < c; ++p) live[size_t(p)] = double(d.fx[size_t(p)]);
    for (int t = 0; t < d.trackCount; ++t) {
        const uint32_t m = lockMask[t];
        if (m == 0) continue;
        for (int p = 0; p < c; ++p)
            if (m & (uint32_t(1) << uint32_t(p))) live[size_t(p)] = double(lockVal[t * c + p]);
    }
    // FX дорожек: строка клипа, который играет (нет клипа — общие значения снимка).
    const int slots = Params::patternSlots;
    trackCount = d.trackCount;
    for (int t = 0; t < d.trackCount; ++t) {
        double* row = tlive.data() + t * c;
        const int k = d.slotRef[t];
        const float* src = k >= 0 && k < slots ? d.clipFx.data() + (t * slots + k) * c : d.fx.data();
        const uint32_t m = lockMask[t];
        for (int p = 0; p < c; ++p)
            row[p] = (m & (uint32_t(1) << uint32_t(p))) ? double(lockVal[t * c + p]) : double(src[p]);
    }
    // LFO/ENV на ручках FX: источник — дорожка, выбранная при назначении.
    if (mod) {
        for (int p = 0; p < c; ++p) {
            const int src = int(d.fxSrc[size_t(p)]);
            const int tr = int(d.fxTrack[size_t(p)]);
            if (src != 0 && tr < d.trackCount) {
                const ParamScale& sc = d.fxScale[size_t(p)];
                const double sv = double(d.fxAmt[size_t(p)]) * mod->source(tr, src, frame);
                const double n = sc.toNorm(live[size_t(p)]) + sv;
                live[size_t(p)] = sc.fromNorm(std::min(1.0, std::max(0.0, n)));
                double* row = tlive.data() + tr * c;
                const double nt = sc.toNorm(row[p]) + sv;
                row[p] = sc.fromNorm(std::min(1.0, std::max(0.0, nt)));
            }
        }
    }
    source = int(std::round(live[scSource]));
    anyDucked = false;
    for (int t = 0; t < d.trackCount; ++t)
        if (t != source && d.scAmount[size_t(d.s(t))] > 0) anyDucked = true;
}

void FxRack::noteTrig(int t, int64_t frame) {
    if (t != source || !anyDucked) return;
    duck.trig(frame, live[scAttack], live[scRelease], live[scCurve]);
}

void FxRack::prepare(int64_t blockStart, int n) {
    duck.process(blockStart, e.data(), n);
    std::fill(fed.begin(), fed.end(), 0);
}

void FxRack::process(int n, double bpm, float* left, float* right) {
    int jt = 0;
    for (int t = 0; t < trackCount; ++t) {
        const size_t b = size_t(t * 3);
        if (fed[size_t(t)] != 0 || idle[b] == 0 || idle[b + 1] == 0 || idle[b + 2] == 0) jobTracks[size_t(jt++)] = t;
    }
    if (jt == 0) return;
    jobN = n;
    jobBpm = bpm;
    if (pool && jt > 1) {
        pool->run(jt, *this);
    } else {
        for (int j = 0; j < jt; ++j) processTrack(jobTracks[size_t(j)]);
    }
    const int mf = maxFrames;
    for (int j = 0; j < jt; ++j) {
        const float* ol = outL.data() + jobTracks[size_t(j)] * mf;
        const float* orr = outR.data() + jobTracks[size_t(j)] * mf;
        for (int i = 0; i < n; ++i) {
            left[i] += ol[i];
            right[i] += orr[i];
        }
    }
}

void FxRack::processTrack(int t) {
    const int n = jobN;
    const int mf = maxFrames;
    const int c = Params::fxCount;
    const double* lv = tlive.data() + t * c;
    const float lvl = idleLevel;
    float* ol = outL.data() + t * mf;
    float* orr = outR.data() + t * mf;
    float* xl = tmpL.data() + t * mf;
    float* xr = tmpR.data() + t * mf;
    std::fill(ol, ol + n, 0.f);
    std::fill(orr, orr + n, 0.f);
    const bool isFed = fed[size_t(t)] != 0;
    const size_t b = size_t(t * 3);
    const int i0 = t * 3 * mf;
    // Reverb.
    {
        const float* rL = isFed ? inL.data() + i0 : nullptr;
        const float* rR = isFed ? inR.data() + i0 : nullptr;
        const float in = isFed ? peak(rL, rR, n) : 0;
        if (idle[b] != 0 && in > lvl) {
            idle[b] = 0;
            revQuiet[size_t(t)] = 0;
        }
        if (idle[b] == 0) {
            reverb[size_t(t)]->process(rL, rR, xl, xr, n, float(lv[revSize]), float(lv[revDecay]), float(lv[revTone]),
                                       float(lv[revHpf]), float(lv[revPredelay]));
            add(n, 1, xl, xr, ol, orr);
            revQuiet[size_t(t)] = in <= lvl && peak(xl, xr, n) <= lvl ? revQuiet[size_t(t)] + n : 0;
            if (double(revQuiet[size_t(t)]) >= revIdleSec * sr) {
                idle[b] = 1;
                reverb[size_t(t)]->sleep();
            }
        }
    }
    // Delay.
    {
        const float* dL = isFed ? inL.data() + i0 + mf : nullptr;
        const float* dR = isFed ? inR.data() + i0 + mf : nullptr;
        const float in = isFed ? peak(dL, dR, n) : 0;
        if (idle[b + 1] != 0 && in > lvl) {
            idle[b + 1] = 0;
            dlyQuiet[size_t(t)] = 0;
        }
        if (idle[b + 1] == 0) {
            const int di = std::max(0, std::min(delayDivs - 1, int(std::round(lv[dlyTime]))));
            // Dly Sync выключен — время в мс (10…2000 по экспоненте, как Params::dlyFreeMs).
            const bool synced = lv[dlySync] >= 0.5;
            const double freeX = std::min(1.0, std::max(0.0, lv[dlyTime] / Params::dlyFreeTop));
            const double freeSec =
                Params::dlyFreeMin * std::pow(Params::dlyFreeMax / Params::dlyFreeMin, freeX) / 1000;
            const double time =
                std::min(maxDelay(), synced ? delayBeats[size_t(di)] * 60 / std::max(1.0, jobBpm) : freeSec);
            const float fb = float(lv[dlyFeedback] / 100);
            delay[size_t(t)]->process(dL, dR, xl, xr, n, float(time), fb, float(lv[dlyFilter] / 100),
                                      float(lv[dlySpread] / 100));
            add(n, 1, xl, xr, ol, orr);
            dlyQuiet[size_t(t)] = in <= lvl && peak(xl, xr, n) <= lvl ? dlyQuiet[size_t(t)] + n : 0;
            if (double(dlyQuiet[size_t(t)]) >= dlyIdleSec * sr) {
                idle[b + 1] = 1;
                delay[size_t(t)]->sleep();
            }
        }
    }
    // Drive.
    {
        const float* vL = isFed ? inL.data() + i0 + 2 * mf : nullptr;
        const float* vR = isFed ? inR.data() + i0 + 2 * mf : nullptr;
        const float in = isFed ? peak(vL, vR, n) : 0;
        if (idle[b + 2] != 0 && in > lvl) {
            idle[b + 2] = 0;
            odQuiet[size_t(t)] = 0;
        }
        if (idle[b + 2] == 0) {
            drive[size_t(t)]->process(vL, vR, xl, xr, n, float(lv[odDrive]), float(lv[odType]), float(lv[odTone]),
                                      float(lv[odLowcut]));
            add(n, driveOut, xl, xr, ol, orr);
            odQuiet[size_t(t)] = in <= lvl && peak(xl, xr, n) <= lvl ? odQuiet[size_t(t)] + n : 0;
            if (double(odQuiet[size_t(t)]) >= odIdleSec * sr) {
                idle[b + 2] = 1;
                drive[size_t(t)]->reset();
            }
        }
    }
}

}  // namespace jambo
