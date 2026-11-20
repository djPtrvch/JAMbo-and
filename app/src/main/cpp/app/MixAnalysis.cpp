#include "MixAnalysis.h"
#include <cfloat>
#include <cmath>

namespace jambo {

PowerFFT::PowerFFT() : window(size), input(size), re(bins), im(bins) {
    for (int i = 0; i < size; i++) {
        const double x = 2 * M_PI * double(i) / double(size);
        window[size_t(i)] = float(0.5 - 0.5 * std::cos(x));
    }
}

void PowerFFT::bands(const float* x, const std::vector<int>& band, std::vector<float>& out) {
    for (int i = 0; i < size; i++) input[size_t(i)] = x[i] * window[size_t(i)];
    fft.forward(input.data(), re.data(), im.data());
    std::fill(out.begin(), out.end(), 0.f);
    for (int k = 1; k < bins; k++) {
        const int b = band[size_t(k)];
        if (b >= 0) out[size_t(b)] += re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)];
    }
}

std::vector<int> PowerFFT::bandMap(const std::vector<float>& edges) {
    std::vector<int> m(bins, -1);
    for (int k = 1; k < bins; k++) {
        const float f = float(k) * hz;
        for (size_t j = 0; j + 1 < edges.size(); j++)
            if (f >= edges[j] && f < edges[j + 1]) m[size_t(k)] = int(j);
    }
    return m;
}

static double biquadPower(const double b[3], const double a[2], double w) {
    const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    const double nr = b[0] + b[1] * c1 + b[2] * c2;
    const double ni = -(b[1] * s1 + b[2] * s2);
    const double dr = 1 + a[0] * c1 + a[1] * c2;
    const double di = -(a[0] * s1 + a[1] * s2);
    return (nr * nr + ni * ni) / (dr * dr + di * di);
}

double MixAnalysis::kPower(double f) {
    const double w = 2 * M_PI * f / 48000;
    const double sb[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285}, sa[2] = {-1.69065929318241, 0.73248077421585};
    const double hb[3] = {1, -2, 1}, ha[2] = {-1.99004745483398, 0.99007225036621};
    return biquadPower(sb, sa, w) * biquadPower(hb, ha, w);
}

MixAnalysis::MixAnalysis()
    : power(tracks, std::vector<float>(bands, 0.f)), mStr(tracks, std::vector<float>(bands, 0.f)),
      mBur(tracks, std::vector<bool>(bands, false)) {
    std::vector<float> e;
    for (int j = 0; j <= bands; j++) e.push_back(20 * std::pow(1000.f, float(j) / float(bands)));
    map = PowerFFT::bandMap(e);
    for (int j = 0; j < bands; j++) kWeight.push_back(float(kPower(20 * std::pow(1000.0, (double(j) + 0.5) / bands))));
}

void MixAnalysis::reset() {
    for (int t = 0; t < tracks; t++) {
        rms[size_t(t)] = kms[size_t(t)] = 0;
        mWho[size_t(t)] = whoCand[size_t(t)] = -1;
        whoHold[size_t(t)] = 0;
        std::fill(mStr[size_t(t)].begin(), mStr[size_t(t)].end(), 0.f);
        std::fill(mBur[size_t(t)].begin(), mBur[size_t(t)].end(), false);
        std::fill(power[size_t(t)].begin(), power[size_t(t)].end(), 0.f);
    }
}

void MixAnalysis::tick(const TrackTaps& taps, int count, const std::vector<bool>& audible, bool mask) {
    if (count <= 0) return;
    const float a = std::exp(-float(count) / 480);
    for (int k = 0; k < 2; k++) {
        const int t = next % count;
        next = (t + 1) % count;
        analyze(taps, t, a);
    }
    if (mask) updateMask(count, audible);
}

void MixAnalysis::analyze(const TrackTaps& taps, int t, float a) {
    const int n = PowerFFT::size;
    taps.read(t, raw.data(), n);
    float sq = 0;
    for (int i = 0; i < n; i++) sq += raw[size_t(i)] * raw[size_t(i)];
    const float r = std::sqrt(sq / float(n));
    rms[size_t(t)] = rms[size_t(t)] * a + r * (1 - a);
    const float ms = sq / float(n);
    if (r < 0.001f) {
        kms[size_t(t)] = kms[size_t(t)] * a + ms * (1 - a);
        return;
    }
    fftp.bands(raw.data(), map, fresh);
    auto& p = power[size_t(t)];
    for (int j = 0; j < bands; j++) p[size_t(j)] = p[size_t(j)] == 0 ? fresh[size_t(j)] : p[size_t(j)] * a + fresh[size_t(j)] * (1 - a);
    float all = 0, wk = 0;
    for (int j = 0; j < bands; j++) {
        all += fresh[size_t(j)];
        wk += fresh[size_t(j)] * kWeight[size_t(j)];
    }
    const float k = all > 0 ? ms * wk / all : ms;
    kms[size_t(t)] = kms[size_t(t)] == 0 ? k : kms[size_t(t)] * a + k * (1 - a);
}

std::optional<MixAnalysis::Span> MixAnalysis::span(int t) const {
    if (t < 0 || t >= tracks || rms[size_t(t)] < 0.0005f) return std::nullopt;
    const auto& p = power[size_t(t)];
    int best = 0;
    for (int j = 0; j < bands; j++)
        if (p[size_t(j)] > p[size_t(best)]) best = j;
    if (p[size_t(best)] <= 0) return std::nullopt;
    const float thr = p[size_t(best)] * std::pow(10.f, -range / 10);
    int lo = best, hi = best;
    for (int j = 0; j < bands; j++)
        if (p[size_t(j)] >= thr) {
            lo = std::min(lo, j);
            hi = std::max(hi, j);
        }
    const float nb = float(bands);
    return Span{float(lo) / nb, float(hi + 1) / nb, (float(best) + 0.5f) / nb};
}

float MixAnalysis::loud(int t) const {
    if (t < 0 || t >= tracks) return 0;
    const float v = kms[size_t(t)];
    if (v <= 1e-9f) return 0;
    return std::sqrt(v * 1.7058f);
}

MixAnalysis::Mask MixAnalysis::mask(int t) const {
    Mask m{std::vector<float>(bands, 0.f), std::vector<bool>(bands, false), -1};
    if (t < 0 || t >= tracks) return m;
    for (int j = 0; j < bands; j++) m.buried[size_t(j)] = mBur[size_t(t)][size_t(j)] && mStr[size_t(t)][size_t(j)] >= maskShow;
    m.strength = mStr[size_t(t)];
    m.who = mWho[size_t(t)];
    return m;
}

void MixAnalysis::updateMask(int count, const std::vector<bool>& audible) {
    const int nb = bands, tn = tracks;
    const float sig = std::pow(10.f, -maskSig / 10);
    const float k = 0.01105f;
    const float own = std::pow(10.f, -range / 10);
    std::vector<float> thr(size_t(tn), FLT_MAX), ownThr(size_t(tn), FLT_MAX);
    std::vector<bool> live(size_t(tn), false);
    for (int u = 0; u < std::min(count, tn); u++) {
        const bool ok = u < int(audible.size()) && audible[size_t(u)] && rms[size_t(u)] >= 0.0005f;
        float top = 0;
        for (int j = 0; j < nb; j++) top = std::max(top, power[size_t(u)][size_t(j)]);
        if (top > 0) {
            thr[size_t(u)] = top * sig;
            ownThr[size_t(u)] = top * own;
        }
        live[size_t(u)] = ok && top > 0;
    }
    for (int t = 0; t < tn; t++) {
        const bool on = t < count && rms[size_t(t)] >= 0.0005f && ownThr[size_t(t)] < FLT_MAX;
        float bestScore = 0;
        int bestBand = -1;
        for (int j = 0; j < nb; j++) {
            float r = 0;
            bool bur = false;
            const float pj = power[size_t(t)][size_t(j)];
            if (on && pj >= ownThr[size_t(t)]) {
                float other = 0;
                for (int u = 0; u < tn; u++)
                    if (u != t && live[size_t(u)] && power[size_t(u)][size_t(j)] >= thr[size_t(u)]) other += power[size_t(u)][size_t(j)];
                if (other > 0) {
                    const float d = 10 * std::log10(other / pj);
                    r = d >= 0 ? 1 : std::max(0.f, 1 + d / maskRange);
                    bur = mBur[size_t(t)][size_t(j)] ? d >= -maskHyst : d >= 0;
                }
            }
            mBur[size_t(t)][size_t(j)] = bur;
            const float v = mStr[size_t(t)][size_t(j)] + (r - mStr[size_t(t)][size_t(j)]) * k;
            mStr[size_t(t)][size_t(j)] = v;
            const float score = v * pj;
            if (v >= 0.3f && score > bestScore) {
                bestScore = score;
                bestBand = j;
            }
        }
        int cand = -1;
        if (bestBand >= 0) {
            float loudest = 0;
            for (int u = 0; u < tn; u++)
                if (u != t && live[size_t(u)] && power[size_t(u)][size_t(bestBand)] >= thr[size_t(u)] &&
                    power[size_t(u)][size_t(bestBand)] > loudest) {
                    loudest = power[size_t(u)][size_t(bestBand)];
                    cand = u;
                }
        }
        if (cand == mWho[size_t(t)]) {
            whoHold[size_t(t)] = 0;
        } else if (cand == whoCand[size_t(t)]) {
            whoHold[size_t(t)] += 1;
            if (whoHold[size_t(t)] >= 60 || (mWho[size_t(t)] < 0 && cand >= 0 && whoHold[size_t(t)] >= 30)) {
                mWho[size_t(t)] = cand;
                whoHold[size_t(t)] = 0;
            }
        } else {
            whoCand[size_t(t)] = cand;
            whoHold[size_t(t)] = 1;
        }
    }
}

}  // namespace jambo
