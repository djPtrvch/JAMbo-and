// EQ дорожек, метры, спектры и замеры колбэка в сэмплере (порт расширения Sampler из Core/EQ/TrackEqs.swift и частей
// SamplerMix.swift: parallelEqs, Levels, Spectra, RenderProfile).
#include <algorithm>
#include "Sampler.h"
#include "engine/Levels.h"
#include "eq/EqDyn.h"
#include "eq/EqListen.h"
#include "eq/Spectrum.h"
#include "eq/TrackEqs.h"
#include "util/RenderProfile.h"

namespace jambo {

namespace {
/// Источники T1…T16 динамических полос — выходы дорожек.
void sources(EqUnit& u, const float* l, const float* r) {
    u.trackL = l;
    u.trackR = r;
    u.trackStride = Sampler::maxFrames;
}
}  // namespace

void Sampler::wakeEqs(const SoundData& d, int n) {
    if (!eqs) return;
    for (int t = 0; t < d.trackCount; ++t) {
        if (used[size_t(t)] != 0 || !eqs->ringing(t)) continue;
        std::fill(busL.begin() + t * maxFrames, busL.begin() + t * maxFrames + n, 0.f);
        std::fill(busR.begin() + t * maxFrames, busR.begin() + t * maxFrames + n, 0.f);
        clearSends(t, n);
        used[size_t(t)] = 1;
    }
}

void Sampler::idleEq(int t, const SoundData& d, int n) {
    if (eqs) {
        EqUnit& u = eqs->unit(t);
        sources(u, outL.data(), outR.data());
        u.idle(d.eqParams(t), n);
        if (eqDyn) eqDyn->publish(t, u);
    }
    if (eqListen) eqListen->idle(t);
    if (!spectra) return;
    if (spectra->channel.load() == t) {
        spectra->pre.pushSilence(n);
        spectra->post.pushSilence(n);
    }
    if (spectra->compareTrack.load() == t) spectra->compare.pushSilence(n);
}

void Sampler::trackEq(int t, const SoundData& d, int n, bool sends) {
    if (!eqs) return;
    EqUnit& u = eqs->unit(t);
    sources(u, outL.data(), outR.data());
    const int mf = maxFrames;
    float* bl = busL.data() + t * mf;
    float* br = busR.data() + t * mf;
    u.laneL[0] = bl;
    u.laneR[0] = br;
    for (int k = 0; k < 3; ++k) {
        u.laneL[size_t(k + 1)] = sends ? sendL.data() + (t * 3 + k) * mf : nullptr;
        u.laneR[size_t(k + 1)] = sends ? sendR.data() + (t * 3 + k) * mf : nullptr;
    }
    const bool shown = spectra && spectra->channel.load() == t;
    if (shown) spectra->pre.push(bl, br, n);
    u.process(n, d.eqParams(t));
    if (eqDyn) eqDyn->publish(t, u);
    if (shown) spectra->post.push(bl, br, n);
    if (spectra && spectra->compareTrack.load() == t) spectra->compare.push(bl, br, n);
    // Прослушивание полосы — после спектров.
    if (eqListen) eqListen->process(t, u, n);
}

void Sampler::parallelEqs(const SoundData& d, int n, bool sends) {
    std::fill(eqDone.begin(), eqDone.end(), 0);
    if (!pool || pool->helperCount() <= 0 || !eqs) return;
    const long shown = spectra ? spectra->channel.load() : -1;
    const long cmp = spectra ? spectra->compareTrack.load() : -1;
    const long lis = eqListen ? eqListen->channel.load() : -1;
    const int lisA = eqListen ? eqListen->active : -1;
    int jt = 0;
    for (int t = 0; t < d.trackCount; ++t)
        if (used[size_t(t)] != 0 && d.eqDep[size_t(t)] == 0 && t != shown && t != cmp && t != lis && t != lisA)
            eqJobs[size_t(jt++)] = t;
    if (jt < 2) return;
    eqJobData = &d;
    eqJobN = n;
    eqJobSends = sends;
    pool->run(jt, *this);
    for (int j = 0; j < jt; ++j) eqDone[size_t(eqJobs[size_t(j)])] = 1;
}

void Sampler::lap(int section) {
    if (profile) profile->lap(section);
}

float* Sampler::meterOf(int t) { return levels ? levels->current(t) : nullptr; }

bool Sampler::loudOf(int t) { return levels ? levels->loudness.listens(t) : false; }

void Sampler::loudProcess(const float* l, const float* r, int n) {
    if (levels) levels->loudness.process(l, r, n);
}

void Sampler::tapTracks(const SoundData& d, int n) {
    if (levels && levels->trackTaps.on.load() != 0)
        levels->trackTaps.push(outL.data(), outR.data(), maxFrames, d.trackCount, n);
}

}  // namespace jambo
