#include "TextureVoice.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace jambo {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double smooth(double a, double b, double x) {
    const double t = clampd((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline int64_t xr(int64_t r0) {
    int64_t r = r0;
    r ^= (r << 13) & 0xFFFFFFFFLL;
    r ^= r >> 17;
    r ^= (r << 5) & 0xFFFFFFFFLL;
    return r;
}
inline double flush(double x) { return (x > 1e-20 || x < -1e-20) ? x : 0; }
constexpr double wDark[4] = {1, 0.25, 0.08, 0.02};
constexpr double wBright[4] = {0.55, 0.85, 0.9, 0.8};
constexpr double ratios[4] = {1, 2, 3, 4};
constexpr double ratiosMetal[4] = {1, 2.76, 5.40, 8.93};
}  // namespace

namespace TextureMap {
double rate(double g) { return 5 * std::pow(30.0, g); }
double length(double g) { return std::max(0.003, 0.012 * std::pow(0.25, g)); }
double gain(double g) { return 2.2 * smooth(0.04, 0.6, g); }
double lpHz(double c, double sr) { return std::min(250 * std::pow(72.0, smooth(0, 0.55, c)), sr * 0.45); }
double hpHz(double c) { return 30 * std::pow(200.0, smooth(0.55, 1, c)); }
double q(double tone) { return 2 + tone * tone * 98; }
double weight(int i, double c) { return lerp(wDark[i], wBright[i], c); }
double ratio(int i, double m) { return lerp(ratios[i], ratiosMetal[i], m); }
const char* colorName(double c) {
    if (c < 0.15) return "brown";
    if (c < 0.35) return "pink";
    if (c < 0.6) return "white";
    if (c < 0.82) return "light";
    return "blue";
}
}  // namespace TextureMap

TextureVoice::TextureVoice(double sampleRate, int seed) : sr_(sampleRate) {
    rnd_ = int64_t((uint64_t(int64_t(seed)) * 2654435761ULL) & 0xFFFFFFFFULL);
    if (rnd_ == 0) rnd_ = 1;
    nC_ = xr(rnd_ ^ 0x5bd1e995LL) | 1;
    nL_ = xr(nC_ ^ 0x2545f491LL) | 1;
    nR_ = xr(nL_ ^ 0x68e31da4LL) | 1;
    for (auto& r : eRnd_) r = 1;
}

double TextureVoice::u() {
    rnd_ = xr(rnd_);
    return double(rnd_) / 4294967296.0;
}

void TextureVoice::set(double n, double q1, double q2, double q3, double q4) {
    note_ = n;
    color_ = clampd(q1, 0, 1);
    grit_ = clampd(q2, 0, 1);
    tone_ = clampd(q3, 0, 1);
    metal_ = clampd(q4, 0, 1);
}

void TextureVoice::trigger(double n, double q1, double q2, double q3, double q4) {
    set(n, q1, q2, q3, q4);
    if (!spawning_) {
        nextAt_ = 0.003 * sr_;
        spawning_ = true;
    }
}

void TextureVoice::silence() {
    events_ = 0;
    spawning_ = false;
    fresh_ = true;
    std::memset(zs_, 0, sizeof zs_);
}

void TextureVoice::coef(int f, double hz, double q) {
    const double w0 = 2 * kPi * std::min(hz, sr_ * 0.45) / sr_;
    const double c = std::cos(w0);
    const double s = std::sin(w0);
    const double alpha = f < 2 ? s / (2 * std::pow(10.0, q / 20)) : s / (2 * q);
    const double a0 = 1 + alpha;
    double* p = co_ + f * 5;
    if (f == 0) {
        p[0] = (1 - c) / 2 / a0; p[1] = (1 - c) / a0; p[2] = p[0];
    } else if (f == 1) {
        p[0] = (1 + c) / 2 / a0; p[1] = -(1 + c) / a0; p[2] = p[0];
    } else {
        p[0] = alpha / a0; p[1] = 0; p[2] = -alpha / a0;
    }
    p[3] = -2 * c / a0;
    p[4] = (1 - alpha) / a0;
}

void TextureVoice::targets(double k) {
    const double c = color_, g = grit_, tn = tone_, m = metal_;
    const double lp = TextureMap::lpHz(c, sr_);
    const double hp = TextureMap::hpHz(c);
    const double band = std::max(300.0, lp - hp) / (sr_ * 0.5);
    const double mk = clampd(0.2 / (0.577 * std::sqrt(band)), 0.5, 6) * (1 - 0.15 * smooth(0.2, 1, g));
    const double q = TextureMap::q(tn);
    const double f0 = 440 * std::pow(2.0, (note_ - 69) / 12);
    double sum = 0;
    for (int i = 0; i < 4; ++i) {
        const double w = TextureMap::weight(i, c);
        const double ra = TextureMap::ratio(i, m);
        sum += w * w * ra;
        sW_[i] += (w - sW_[i]) * k;
        const double fr = std::min(f0 * ra, sr_ * 0.45);
        sF_[i] += (fr - sF_[i]) * k;
    }
    const double half = tn * kPi / 2;
    const double wet = std::sin(half) * std::min(60.0, 37.5 * std::sqrt(q / (f0 * sum)));
    sLp_ += (lp - sLp_) * k;
    sHp_ += (hp - sHp_) * k;
    sMk_ += (mk - sMk_) * k;
    sDry_ += (std::cos(half) - sDry_) * k;
    sWet_ += (wet - sWet_) * k;
    sQ_ += (q - sQ_) * k;
    coef(0, sLp_, -3);
    coef(1, sHp_, -3);
    for (int i = 0; i < 4; ++i) coef(2 + i, sF_[i], sQ_);
}

void TextureVoice::spawn(int delay, double rate, double len, double gain) {
    if (events_ >= TextureMap::maxEvents) return;
    const int k = events_++;
    const double amp = (0.5 + 0.5 * u()) * gain / std::sqrt(std::max(1.0, rate * len));
    const double pan = (u() * 2 - 1) * 0.7 * TextureMap::width;
    const double x = (pan + 1) / 2 * kPi / 2;
    eEnv_[k] = 0;
    eAmp_[k] = amp;
    ePos_[k] = -delay;
    eLen_[k] = int(std::round((2 * len + 0.004) * sr_));
    eK_[k] = std::exp(-1 / (len / 3 * sr_));
    eL_[k] = std::cos(x);
    eR_[k] = std::sin(x);
    int64_t r = xr(rnd_ ^ 0x2545f491LL);
    if (r == 0) r = 3;
    eRnd_[k] = r;
    rnd_ = xr(rnd_);
}

void TextureVoice::render(float* outL, float* outR, int n, int window) {
    const double gOut = TextureMap::out;
    const double prevMk = sMk_ * sDry_ * gOut, prevWet = sWet_ * gOut;
    targets(fresh_ ? 1 : 1 - std::exp(-double(n) / (0.03 * sr_)));
    fresh_ = false;
    double* bl = busL_;
    double* br = busR_;
    // Подложка: L = bed·(gc·C + gl·L), R = bed·(gc·C + gr·R).
    const double wa = TextureMap::width * kPi / 2;
    const double gc = TextureMap::bed * std::cos(wa), gs = TextureMap::bed * std::sin(wa);
    int64_t c0 = nC_, l0 = nL_, r0 = nR_;
    for (int j = 0; j < n; ++j) {
        c0 = xr(c0); l0 = xr(l0); r0 = xr(r0);
        const double c = (double(c0) / 2147483648.0 - 1) * gc;
        bl[j] = c + (double(l0) / 2147483648.0 - 1) * gs;
        br[j] = c + (double(r0) / 2147483648.0 - 1) * gs;
    }
    nC_ = c0; nL_ = l0; nR_ = r0;
    // Щелчки Grit: поток Пуассона внутри окна активности.
    if (spawning_) {
        const double g = grit_;
        const double gain = TextureMap::gain(g), rate = TextureMap::rate(g), len = TextureMap::length(g);
        while (nextAt_ < double(n)) {
            const int at = int(nextAt_ < 0 ? 0 : nextAt_);
            if (at >= window) { spawning_ = false; break; }
            if (gain > 0) spawn(at, rate, len, gain);
            nextAt_ += std::max(0.0005, -std::log(1 - u()) / rate) * sr_;
        }
        nextAt_ -= double(n);
        if (window <= n) spawning_ = false;
    }
    const double attN = 0.0015 * sr_;
    int e = 0;
    while (e < events_) {
        int pos = ePos_[e];
        const int len = eLen_[e];
        double env = eEnv_[e];
        const double amp = eAmp_[e], k = eK_[e], gl = eL_[e], gr = eR_[e];
        int64_t r = eRnd_[e];
        int i = 0;
        if (pos < 0) { i = -pos; pos = 0; }
        // Нарастание линейно за 1,5 мс до amp, затем спад (τ = len/3); обрыв через 2·len + 4 мс.
        double pd = double(pos);
        while (i < n && pos < len) {
            if (pd < attN) {
                env = amp * (pd + 1) / attN;
                if (env > amp) env = amp;
            } else {
                env *= k;
            }
            r = xr(r);
            const double x = (double(r) / 2147483648.0 - 1) * env;
            bl[i] += x * gl;
            br[i] += x * gr;
            ++pos; pd += 1; ++i;
        }
        if (pos >= len) {
            --events_;
            if (e != events_) {
                eEnv_[e] = eEnv_[events_]; eAmp_[e] = eAmp_[events_]; ePos_[e] = ePos_[events_];
                eLen_[e] = eLen_[events_]; eK_[e] = eK_[events_]; eL_[e] = eL_[events_];
                eR_[e] = eR_[events_]; eRnd_[e] = eRnd_[events_];
            }
            continue;
        }
        ePos_[e] = pos;
        eEnv_[e] = env;
        eRnd_[e] = r;
        ++e;
    }
    filter(outL, outR, n, prevMk, prevWet);
}

void TextureVoice::filter(float* outL, float* outR, int n, double prevDry, double prevWet) {
    const double g = TextureMap::out;
    const double dryT = sMk_ * sDry_ * g;
    const double dd = (dryT - prevDry) / double(n);
    const double dw = (sWet_ * g - prevWet) / double(n);
    const double w0 = sW_[0], w1 = sW_[1], w2 = sW_[2], w3 = sW_[3];
    const double* c = co_;
    for (int ch = 0; ch < 2; ++ch) {
        const double* bus = ch == 0 ? busL_ : busR_;
        float* out = ch == 0 ? outL : outR;
        double* z = zs_ + ch * 2;
        double lz1 = z[0], lz2 = z[1], hz1 = z[4], hz2 = z[5];
        double az1 = z[8], az2 = z[9], bz1 = z[12], bz2 = z[13];
        double cz1 = z[16], cz2 = z[17], ez1 = z[20], ez2 = z[21];
        double gd = prevDry, gw = prevWet;
        for (int j = 0; j < n; ++j) {
            const double x = bus[j];
            gd += dd;
            gw += dw;
            // Сухой: ФНЧ → ФВЧ (TDF II).
            const double y0 = c[0] * x + lz1;
            lz1 = c[1] * x - c[3] * y0 + lz2;
            lz2 = c[2] * x - c[4] * y0;
            const double y1 = c[5] * y0 + hz1;
            hz1 = c[6] * y0 - c[8] * y1 + hz2;
            hz2 = c[7] * y0 - c[9] * y1;
            // Звенящий: 4 полосовых (b1 = 0).
            const double r0 = c[10] * x + az1;
            az1 = -c[13] * r0 + az2;
            az2 = c[12] * x - c[14] * r0;
            const double r1 = c[15] * x + bz1;
            bz1 = -c[18] * r1 + bz2;
            bz2 = c[17] * x - c[19] * r1;
            const double r2 = c[20] * x + cz1;
            cz1 = -c[23] * r2 + cz2;
            cz2 = c[22] * x - c[24] * r2;
            const double r3 = c[25] * x + ez1;
            ez1 = -c[28] * r3 + ez2;
            ez2 = c[27] * x - c[29] * r3;
            const double wet = (r0 * w0 + r1 * w1 + r2 * w2 + r3 * w3) * gw;
            out[j] = float(y1 * gd + wet);
        }
        // NaN и денормалы: при сбое состояние — с нуля, кусок — тишина.
        const double sum = lz1 + lz2 + hz1 + hz2 + az1 + az2 + bz1 + bz2 + cz1 + cz2 + ez1 + ez2;
        if (!std::isfinite(sum)) {
            for (int f = 0; f < 6; ++f) { z[f * 4] = 0; z[f * 4 + 1] = 0; }
            std::memset(out, 0, sizeof(float) * n);
        } else {
            z[0] = flush(lz1); z[1] = flush(lz2); z[4] = flush(hz1); z[5] = flush(hz2);
            z[8] = flush(az1); z[9] = flush(az2); z[12] = flush(bz1); z[13] = flush(bz2);
            z[16] = flush(cz1); z[17] = flush(cz2); z[20] = flush(ez1); z[21] = flush(ez2);
        }
    }
}

}  // namespace jambo
