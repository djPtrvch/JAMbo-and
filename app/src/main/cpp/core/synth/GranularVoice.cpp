#include <cstdint>
#include "GranularVoice.h"
#include "TexGran.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace jambo {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline int64_t xr(int64_t r0) {
    int64_t r = r0;
    r ^= (r << 13) & 0xFFFFFFFFLL;
    r ^= r >> 17;
    r ^= (r << 5) & 0xFFFFFFFFLL;
    return r;
}
/// Окна: Ханн и Гаусс σ = 0,2 (+ точка за краем для интерполяции).
struct Windows {
    double hann[GranularVoice::winPoints + 1];
    double gauss[GranularVoice::winPoints + 1];
    Windows() {
        const int n = GranularVoice::winPoints;
        for (int i = 0; i < n; ++i) hann[i] = 0.5 * (1 - std::cos(2 * kPi * double(i) / double(n - 1)));
        hann[n] = 0;
        const double sg = 0.2, g0 = std::exp(-0.5 * std::pow(0.5 / sg, 2));
        for (int i = 0; i < n; ++i) {
            const double x = double(i) / double(n - 1);
            gauss[i] = (std::exp(-0.5 * std::pow((x - 0.5) / sg, 2)) - g0) / (1 - g0);
        }
        gauss[n] = 0;
    }
};
const Windows& windows() {
    static const Windows w;
    return w;
}
}  // namespace

double GrainTrack::r2() {
    rnd = xr(rnd);
    return double(rnd) / 2147483648.0 - 1;
}

void GrainTrack::advance(double dt) {
    drHold -= dt;
    if (drHold <= 0) {
        drT = r2();
        drHold += 1.5 + (r2() + 1) * 1.5;
        if (drHold <= 0) drHold = 1.5;
    }
    dr += (drT - dr) * (1 - std::pow(1 - 0.012, dt / 0.025));
}

GranularVoice::GranularVoice(double sampleRate, int seed, GrainTrack* track) : sr_(sampleRate) {
    rnd_ = int64_t((uint64_t(int64_t(seed)) * 2246822519ULL + 77ULL) & 0xFFFFFFFFULL);
    if (rnd_ == 0) rnd_ = 1;
    if (track) {
        track_ = track;
        ownsTrack_ = false;
    } else {
        track_ = new GrainTrack();
        track_->rnd = int64_t(((uint64_t(int64_t(seed)) * 40503ULL + 11ULL) & 0xFFFFFFFFULL) | 1ULL);
        ownsTrack_ = true;
    }
    for (int i = 0; i < maxGrains; ++i) { gCr_[i] = 1; gWr_[i] = 1; }
    windows();
}

GranularVoice::~GranularVoice() {
    if (ownsTrack_) delete track_;
}

double GranularVoice::window(double x, bool g) {
    const double* t = g ? windows().gauss : windows().hann;
    const double ph = clampd(x, 0, 1) * double(winPoints - 1);
    const int i = int(ph);
    return t[i] + (t[i + 1] - t[i]) * (ph - double(i));
}

double GranularVoice::r2() {
    rnd_ = xr(rnd_);
    return double(rnd_) / 2147483648.0 - 1;
}

void GranularVoice::set(double n, double q1, double q2, double q3, double q4) {
    note_ = n;
    position_ = clampd(q1, 0, 1);
    size_ = clampd(q2, 0, 1);
    density_ = clampd(q3, 0, 1);
    spray_ = clampd(q4, 0, 1);
}

void GranularVoice::trigger(double n, double q1, double q2, double q3, double q4) {
    set(n, q1, q2, q3, q4);
    if (!spawning_) {
        head_ = track_->notes > 0 ? track_->head : position_;
        sizeS_ = size_;
        densS_ = density_;
        nextAt_ = 0.003 * sr_;
        clock_ = 0;
        spawning_ = true;
    }
}

void GranularVoice::silence() {
    track_->grains -= grains_;
    if (track_->grains < 0) track_->grains = 0;
    grains_ = 0;
    spawning_ = false;
    fresh_ = true;
    gain_ = 0;
}

void GranularVoice::spawn(double start, const GrainSrc& src, double dur, bool g) {
    if (grains_ >= maxGrains || track_->grains >= maxGrains) return;
    const double len = double(src.frames) / sr_;
    const double sp = spray_;
    const double cents = r2() * TexGran::centsSpread(sp);
    const double rate = std::pow(2.0, (note_ - root) / 12 + cents / 1200);
    double pos = (head_ + track_->dr * 0.03 * (0.3 + sp) + r2() * TexGran::posSpread(sp)) * len;
    const double ts = src.period;
    if (ts > 0) pos = std::round(pos / ts) * ts;
    pos = clampd(pos, 0, std::max(0.0, len - dur * rate - 0.001));
    const double pan = clampd(r2() * TexGran::panSpread(sp), -1, 1);
    const double frames = dur * sr_;
    if (frames < 2) return;
    const int k = grains_++;
    track_->grains += 1;
    const double at = std::ceil(start);
    const double frac = at - start;
    const double phStep = double(winPoints - 1) / frames;
    gPos_[k] = pos * sr_ + frac * rate;
    gStep_[k] = rate;
    gPh_[k] = frac * phStep;
    gPhStep_[k] = phStep;
    const double th = 2 * kPi * frac / frames;
    const double dth = 2 * kPi / frames;
    gCr_[k] = std::cos(th);
    gCi_[k] = std::sin(th);
    gWr_[k] = std::cos(dth);
    gWi_[k] = std::sin(dth);
    gGauss_[k] = g ? 1 : 0;
    gAt_[k] = -int(at);
    double* gg = gG_ + k * 4;
    if (src.channels > 1 && src.right) {
        // Баланс стерео-входа.
        const double x = (pan <= 0 ? pan + 1 : pan) * kPi / 2;
        gg[0] = pan <= 0 ? 1 : std::cos(x);
        gg[1] = pan <= 0 ? std::cos(x) : 0;
        gg[2] = pan <= 0 ? std::sin(x) : 1;
        gg[3] = pan <= 0 ? 0 : std::sin(x);
    } else {
        const double x = (pan + 1) / 2 * kPi / 2;
        gg[0] = std::cos(x);
        gg[1] = std::sin(x);
        gg[2] = 0;
        gg[3] = 0;
    }
    gSrc_[k] = src;
    if (onSpawn) onSpawn(clock_ + start, pos, rate, pan);
}

void GranularVoice::render(float* outL, float* outR, int n, int window, const GrainSrc& src) {
    const bool hasSrc = src.left && src.frames > 1;
    const double len = hasSrc ? double(src.frames) / sr_ : 0;
    // Курсор: постоянная 150 мс.
    const double k150 = 1 - std::exp(-double(n) / (0.15 * sr_));
    head_ += (position_ - head_) * k150;
    sizeS_ += (size_ - sizeS_) * k150;
    densS_ += (density_ - densS_) * k150;
    if (spawning_) track_->head = head_;
    const int notesN = std::max(1, track_->notes);
    const double sc = std::min(1.0, 4.0 / double(notesN));
    const double g0 = gain_;
    if (hasSrc) {
        const auto gs = TexGran::state(sizeS_, densS_, sc, len);
        const auto pl = TexGran::plan(gs.dur, gs.eff, src.period, note_ - root, spray_);
        const double target = pl.gain * TexGran::out;
        const double tau = target > g0 ? std::max(0.08 * sr_, longest_) : 0.08 * sr_;
        gain_ = fresh_ ? target : g0 + (target - g0) * (1 - std::exp(-double(n) / tau));
        fresh_ = false;
        if (spawning_) {
            const double jit = TexGran::jitter(spray_);
            const double ivF = pl.iv * sr_;
            const bool gauss = gs.eff < 2;
            while (nextAt_ < double(n)) {
                const double at = nextAt_ < 0 ? 0 : nextAt_;
                if (at >= double(window)) { spawning_ = false; break; }
                spawn(at, src, gs.dur, gauss);
                nextAt_ += jit > 0 ? ivF * (1 + r2() * jit) : ivF;
            }
        }
    }
    if (spawning_) {
        nextAt_ = hasSrc ? nextAt_ - double(n) : std::max(0.0, nextAt_ - double(n));
        if (window <= n) spawning_ = false;
    }
    clock_ += double(n);
    double* bl = busL_;
    double* br = busR_;
    std::memset(bl, 0, sizeof(double) * n);
    std::memset(br, 0, sizeof(double) * n);
    const double last = double(winPoints - 1);
    double minStep = 1;
    int e = 0;
    while (e < grains_) {
        const GrainSrc& s = gSrc_[e];
        int at = gAt_[e];
        double pos = gPos_[e];
        const double step = gStep_[e];
        double ph = gPh_[e];
        const double phStep = gPhStep_[e];
        if (phStep < minStep) minStep = phStep;
        const double* tab = gGauss_[e] ? windows().gauss : windows().hann;
        double* g = gG_ + e * 4;
        const double ga = g[0], gb = g[1], gc = g[2], gd = g[3];
        const float* lp = s.left;
        const int lastF = s.frames - 1;
        int i = 0;
        if (at < 0) { i = -at; at = 0; }
        const bool hannW = gGauss_[e] == 0;
        // Весь кусок — внутри сэмпла: без проверки на отсчёт.
        const bool safe = int(pos + step * double(n - i)) + 1 < lastF;
        double cr = gCr_[e], ci = gCi_[e];
        const double wr = gWr_[e], wi = gWi_[e];
        if (hannW && safe) {
            if (s.channels > 1 && s.right) {
                const float* rp = s.right;
                while (i < n && ph < last) {
                    const double w = 0.5 - 0.5 * cr;
                    const int ip = int(pos);
                    const double f = pos - double(ip);
                    const double l0 = lp[ip], r0 = rp[ip];
                    const double a = (l0 + (double(lp[ip + 1]) - l0) * f) * w;
                    const double b = (r0 + (double(rp[ip + 1]) - r0) * f) * w;
                    bl[i] += a * ga + b * gb;
                    br[i] += b * gc + a * gd;
                    const double t = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = t;
                    ph += phStep; pos += step; ++i;
                }
            } else {
                while (i < n && ph < last) {
                    const double w = 0.5 - 0.5 * cr;
                    const int ip = int(pos);
                    const double l0 = lp[ip];
                    const double a = (l0 + (double(lp[ip + 1]) - l0) * (pos - double(ip))) * w;
                    bl[i] += a * ga;
                    br[i] += a * gb;
                    const double t = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = t;
                    ph += phStep; pos += step; ++i;
                }
            }
        } else if (s.channels > 1 && s.right) {
            const float* rp = s.right;
            while (i < n && ph < last) {
                const int wi2 = int(ph);
                const double w = tab[wi2] + (tab[wi2 + 1] - tab[wi2]) * (ph - double(wi2));
                const int ip = int(pos);
                const double f = pos - double(ip);
                double a = 0, b = 0;
                if (ip < lastF) {
                    a = double(lp[ip]) + (double(lp[ip + 1]) - double(lp[ip])) * f;
                    b = double(rp[ip]) + (double(rp[ip + 1]) - double(rp[ip])) * f;
                }
                a *= w;
                b *= w;
                bl[i] += a * ga + b * gb;
                br[i] += b * gc + a * gd;
                ph += phStep; pos += step; ++i;
            }
        } else {
            while (i < n && ph < last) {
                const int wi2 = int(ph);
                const double w = tab[wi2] + (tab[wi2 + 1] - tab[wi2]) * (ph - double(wi2));
                const int ip = int(pos);
                const double f = pos - double(ip);
                double a = 0;
                if (ip < lastF) a = double(lp[ip]) + (double(lp[ip + 1]) - double(lp[ip])) * f;
                a *= w;
                bl[i] += a * ga;
                br[i] += a * gb;
                ph += phStep; pos += step; ++i;
            }
        }
        // Медленный путь Ханна (по таблице) — поворот догоняет фазу окна.
        if (hannW && !safe) {
            const double th = 2 * kPi * ph / last;
            cr = std::cos(th);
            ci = std::sin(th);
        }
        if (ph >= last) {
            --grains_;
            track_->grains -= 1;
            if (e != grains_) {
                const int j = grains_;
                gPos_[e] = gPos_[j]; gStep_[e] = gStep_[j]; gPh_[e] = gPh_[j]; gPhStep_[e] = gPhStep_[j];
                gCr_[e] = gCr_[j]; gCi_[e] = gCi_[j]; gWr_[e] = gWr_[j]; gWi_[e] = gWi_[j];
                gGauss_[e] = gGauss_[j]; gAt_[e] = gAt_[j];
                const double* h = gG_ + j * 4;
                g[0] = h[0]; g[1] = h[1]; g[2] = h[2]; g[3] = h[3];
                gSrc_[e] = gSrc_[j];
            }
            continue;
        }
        gAt_[e] = at;
        gPos_[e] = pos;
        gPh_[e] = ph;
        gCr_[e] = cr;
        gCi_[e] = ci;
        ++e;
    }
    longest_ = grains_ > 0 ? last / minStep : 0;
    // Усиление шины — плавно по куску.
    const double dg = (gain_ - g0) / double(n);
    double gg = g0;
    for (int j = 0; j < n; ++j) {
        gg += dg;
        outL[j] = float(bl[j] * gg);
        outR[j] = float(br[j] * gg);
    }
}

}  // namespace jambo
