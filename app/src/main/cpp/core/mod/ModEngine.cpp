// Порт Core/Mod/ModEngine.swift и Core/Mod/EnvDot.swift (ветка swift JAMbo).
#include "ModEngine.h"
#include <cmath>
#include <random>

namespace jambo {

uint32_t ModEngine::randomSeed() {
    std::random_device rd;
    return rd();
}

ModEngine::ModEngine(double sampleRate, uint32_t seed) : sr(sampleRate), rng(seed) {
    const int n = tracks;
    phase.assign(size_t(n * 3), 0);
    rnd.assign(size_t(n * 3), 0);
    lfo.assign(size_t(n * 3), 0);
    p.assign(size_t(n * Params::modCount), 0);
    for (const auto& d : Params::lfoDivs) divBeats.push_back(d.beats);
    envStart.assign(size_t(n), -1);
    envGateEnd.assign(size_t(n), 0);
    live.assign(size_t(n), 0);
    outs.reset(new AtomicFloat[size_t(n * 4)]);
    phaseOuts.reset(new AtomicFloat[size_t(n * 3)]);
    envOuts.reset(new AtomicFloat[size_t(n * 2)]);
    for (int k = 0; k < n * 2; ++k) envOuts[size_t(k)].store(-1);
    const auto& ids = Params::modIds();
    for (int t = 0; t < n; ++t)
        for (int k = 0; k < int(ids.size()); ++k) {
            const ParamDef* d = Params::byId(ids[size_t(k)]);
            p[size_t(t * Params::modCount + k)] = d ? d->def : 0;
        }
}

double ModEngine::shape(int wave, double ph, double r) {
    switch (wave) {
        case 0: return std::sin(ph * 2 * M_PI);
        case 1: return ph < 0.25 ? ph * 4 : (ph < 0.75 ? 2 - ph * 4 : ph * 4 - 4);
        case 2: return ph * 2 - 1;
        case 3: return ph < 0.5 ? 1 : -1;
        default: return r;
    }
}

void ModEngine::begin(const SoundData& d, int n, const uint16_t* lockMask, const float* lockVal, double bpm) {
    data = &d;
    const int c = Params::modCount;
    const double dt = double(n) / sr;
    const double speedMin = Params::lfoSpeedMin;
    const double speedLogRange = std::log(Params::lfoSpeedMax / Params::lfoSpeedMin);
    const int divCount = int(divBeats.size());
    for (int t = 0; t < d.trackCount; ++t) {
        const uint16_t m = lockMask[t];
        // Переход MIX: параметры LFO/ENV — звука, который сейчас действует.
        const int u0 = d.s(t);
        const int su = u0 * c;
        for (int k = 0; k < c; ++k) {
            const int i = t * c + k;
            p[size_t(i)] = (m & (uint16_t(1) << k)) != 0 ? double(lockVal[i]) : double(d.mod[size_t(su + k)]);
        }
        for (int l = 0; l < 3; ++l) {
            const int b = t * c + 4 * l;
            const size_t j = size_t(t * 3 + l);
            const double s = shape(int(std::round(p[size_t(b)])), phase[j], rnd[j]);
            const double u = p[size_t(b + 3)];
            lfo[j] = (s * (1 - u) + ((s + 1) / 2) * u) * (1 - 2 * p[size_t(b + 2)]);
            double hz = p[size_t(b + 1)];
            if (d.lfoSync[size_t(u0 * 3 + l)] != 0) {
                // Пункт — по месту на ходе ручки Speed.
                const double x = std::log(std::max(speedMin, hz) / speedMin) / speedLogRange;
                const double top = double(divCount - 1);
                const int k = std::min(divCount - 1, std::max(0, int(std::round(x * top))));
                hz = std::max(1.0, bpm) / 60 / divBeats[size_t(k)];
            }
            double ph = phase[j] + dt * hz;
            if (ph >= 1) {
                ph = std::fmod(ph, 1.0);
                rnd[j] = rng.next() * 2 - 1;
            }
            phase[j] = ph;
            outs[size_t(t * 4 + l)].store(float(lfo[j]));
            phaseOuts[j].store(float(phase[j]));
        }
    }
}

void ModEngine::publish(int64_t frame) {
    if (!data) return;
    const double ms = 1000 / sr;
    for (int t = 0; t < data->trackCount; ++t) {
        outs[size_t(t * 4 + 3)].store(float(env(t, frame)));
        const int64_t s0 = envStart[size_t(t)];
        envOuts[size_t(t * 2)].store(s0 < 0 ? -1 : float(double(frame - s0) * ms));
        envOuts[size_t(t * 2 + 1)].store(envGateEnd[size_t(t)] == gateLive ? 1e9f
                                                                          : float(double(envGateEnd[size_t(t)] - s0) * ms));
    }
}

double ModEngine::env(int t, int64_t frame) const {
    const int64_t s0 = envStart[size_t(t)];
    if (s0 < 0 || frame < s0) return 0;
    const int b = t * Params::modCount;
    const double a = p[size_t(b + envAttack)];
    const double dd = p[size_t(b + envDecay)];
    const double s = p[size_t(b + envSustain)] / 100;
    const double r = p[size_t(b + envRelease)];
    const double ms = 1000 / sr;
    const double x = double(frame - s0) * ms;
    const double gate = envGateEnd[size_t(t)] == gateLive ? INFINITY : double(envGateEnd[size_t(t)] - s0) * ms;
    auto ads = [&](double y) { return y < a ? y / a : (y < a + dd ? 1 - (1 - s) * (y - a) / dd : s); };
    if (x < gate) return ads(x);
    const double rel = x - gate;
    return rel < r ? ads(gate) * (1 - rel / r) : 0;
}

double ModEngine::source(int t, int lfoN, int64_t frame) const {
    if (lfoN == 4) return env(t, frame);
    if (lfoN < 1 || lfoN > 3) return 0;
    return lfo[size_t(t * 3 + lfoN - 1)];
}

void ModEngine::trig(int t, int64_t frame, int64_t gate) {
    envStart[size_t(t)] = frame;
    envGateEnd[size_t(t)] = gate == gateLive ? gateLive : frame + std::max<int64_t>(0, gate);
}

void ModEngine::liveOn(int t, int64_t frame) {
    live[size_t(t)] += 1;
    trig(t, frame, gateLive);
}

void ModEngine::liveOff(int t, int64_t frame) {
    live[size_t(t)] = std::max(0, live[size_t(t)] - 1);
    if (live[size_t(t)] == 0 && envStart[size_t(t)] >= 0 && envGateEnd[size_t(t)] > frame) envGateEnd[size_t(t)] = frame;
}

double ModEngine::value(int t, int j, double base, int64_t frame) const {
    if (!data) return base;
    const size_t i = size_t(data->s(t) * ModTarget::count + j);
    const int src = int(data->asSrc[i]);
    if (src == 0) return base;
    const ParamScale& sc = data->scale[i];
    const double n = sc.toNorm(base) + double(data->asAmt[i]) * source(t, src, frame);
    return sc.fromNorm(std::min(1.0, std::max(0.0, n)));
}

bool ModEngine::modulated(int t) const {
    if (!data) return false;
    return data->asAny[size_t(data->s(t))] != 0;
}

namespace EnvDot {

double ads(double a, double d, double s, double y) {
    if (y < a) return a > 0 ? y / a : 1;
    if (y < a + d) return d > 0 ? 1 - (1 - s) * (y - a) / d : s;
    return s;
}

double curve(double a, double d, double s, double r, double x, double gate) {
    if (x < 0) return 0;
    if (x < gate) return ads(a, d, s, x);
    const double rel = x - gate;
    return rel < r ? ads(a, d, s, gate) * (1 - rel / r) : 0;
}

double x(double a, double d, double s, double r, double t, double g, double gate) {
    if (t < 0) return 0;
    if (t < g) return std::min(gate, t);
    const double rel = t - g;
    if (rel >= r) return gate + r;
    const double level = ads(a, d, s, g) * (1 - rel / r);
    if (g < a + d && level > s && d > 0 && s < 1) return a + d * (1 - level) / (1 - s);
    if (s <= 0) return gate + r;
    const double k = std::min(1.0, level / s);
    return gate + r * (1 - k);
}

}  // namespace EnvDot
}  // namespace jambo
