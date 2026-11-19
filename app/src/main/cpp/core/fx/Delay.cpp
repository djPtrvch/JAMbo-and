#include "Delay.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>

namespace jambo {

namespace {
// Константы (DELAY_CONST).
constexpr double kLoopHpHz = 60;
constexpr double kSatK = 1.2;
constexpr double kWowHz = 0.5;
constexpr double kWowMs = 0.2;
constexpr double kFlutterHz = 7;
constexpr double kFlutterMs = 0.03;
constexpr double kGlideMs = 80;
constexpr double kGlideMax = 0.75;
constexpr double kSmoothMs = 20;
constexpr double kLpMaxHz = 20000;
constexpr double kLpMinHz = 500;
constexpr double kHpMinHz = 20;
constexpr double kHpMaxHz = 1500;
constexpr double kFilterOff = 0.02;
constexpr double kSpreadMax = 0.5;
constexpr double kTiny = 1e-18;
constexpr double kPi = 3.14159265358979323846;

/// Биквад HP (RBJ, Q = 1/√2): [b0, b1, b2, a1, a2].
void biquadHp(double f, double sr, double* k) {
    const double w = 2 * kPi * std::min(f, sr * 0.45) / sr;
    const double cw = std::cos(w);
    const double al = std::sin(w) / (2 * 0.7071067811865476);
    const double a0 = 1 + al;
    const double b0 = (1 + cw) / 2;
    k[0] = b0 / a0; k[1] = -(1 + cw) / a0; k[2] = b0 / a0; k[3] = (-2 * cw) / a0; k[4] = (1 - al) / a0;
}

/// SVF (TPT) на f, Q = 1/√2: [a1, a2, a3].
void svf(double f, double sr, double* c) {
    const double g = std::tan(kPi * std::min(f, sr * 0.49) / sr);
    const double k = 1.4142135623730951;
    const double a1 = 1 / (1 + g * (g + k));
    const double a2 = g * a1;
    c[0] = a1; c[1] = a2; c[2] = g * a2;
}

inline double dn(double x) { return std::fabs(x) < 1e-20 ? 0 : x; }
inline double clampd(double x, double lo, double hi) { return std::min(hi, std::max(lo, x)); }
}  // namespace

FxDelay::FxDelay(double sampleRate, double maxTime, uint32_t seed)
    : sr_(sampleRate), maxTime_(maxTime), fSig_(std::numeric_limits<double>::quiet_NaN()), rng_(seed) {
    const int need = int(std::ceil(maxTime * (1 + kSpreadMax) * sampleRate
                                   + ((kWowMs + kFlutterMs) / 1000) * sampleRate + 8));
    int p = 1;
    while (p < need) p <<= 1;
    len_ = p;
    mask_ = p - 1;
    // calloc: страницы нулевые и занимают память, только когда в них пишут.
    lineL_ = static_cast<float*>(std::calloc(p, sizeof(float)));
    lineR_ = static_cast<float*>(std::calloc(p, sizeof(float)));
    biquadHp(kLoopHpHz, sampleRate, kHp_);
    wowPh_[0] = rng_.next(); wowPh_[1] = rng_.next();
    wowA_[0] = rng_.next() * 2 - 1; wowA_[1] = rng_.next() * 2 - 1;
    wowB_[0] = rng_.next() * 2 - 1; wowB_[1] = rng_.next() * 2 - 1;
    const double fw = 2 * kPi * kFlutterHz / sampleRate;
    flR_ = std::cos(fw);
    flI_ = std::sin(fw);
    const double p0 = rng_.next() * 2 * kPi;
    const double p1 = rng_.next() * 2 * kPi;
    flS_[0] = std::sin(p0); flS_[1] = std::sin(p1);
    flC_[0] = std::cos(p0); flC_[1] = std::cos(p1);
}

FxDelay::~FxDelay() {
    std::free(lineL_);
    std::free(lineR_);
}

void FxDelay::updateFilter(double f) {
    if (f == fSig_) return;
    fSig_ = f;
    const double lp = f < 0 ? kLpMaxHz * std::pow(kLpMinHz / kLpMaxHz, -f) : kLpMaxHz;
    const double hp = f > 0 ? kHpMinHz * std::pow(kHpMaxHz / kHpMinHz, f) : kHpMinHz;
    svf(lp, sr_, cLp_);
    svf(hp, sr_, cHp_);
}

void FxDelay::sleep() {
    std::memset(zHp_, 0, sizeof zHp_);
    std::memset(sLp_, 0, sizeof sLp_);
    std::memset(sHp_, 0, sizeof sHp_);
    started_ = false;
    dL_ = 0;
    dR_ = 0;
}

void FxDelay::reset() {
    std::memset(lineL_, 0, sizeof(float) * len_);
    std::memset(lineR_, 0, sizeof(float) * len_);
    std::memset(zHp_, 0, sizeof zHp_);
    std::memset(sLp_, 0, sizeof sLp_);
    std::memset(sHp_, 0, sizeof sHp_);
}

void FxDelay::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                      float time, float feedback, float fIn, float spIn) {
    const double sr = sr_;
    const double tTime = std::min(maxTime_, std::max(0.001, double(time)));
    const double tFb = clampd(double(feedback), 0, 1);
    const double tFilter = clampd(double(fIn), -1, 1);
    const double tSpread = clampd(double(spIn), 0, 1);
    if (!started_) {
        started_ = true;
        fb_ = tFb;
        filter_ = tFilter;
        spread_ = tSpread;
    }
    const double a = 1 - std::exp((-3 * double(n)) / ((kSmoothMs / 1000) * sr));
    const double fb0 = fb_;
    const double fb1 = fb0 + (tFb - fb0) * a;
    fb_ = fb1;
    filter_ += (tFilter - filter_) * a;
    if (std::fabs(filter_ - tFilter) < 1e-4) filter_ = tFilter;
    const double sp0 = spread_;
    const double sp1 = sp0 + (tSpread - sp0) * a;
    spread_ = sp1;
    updateFilter(filter_);
    const double off = kFilterOff;
    const double wLp0 = wLp_;
    const double wLp1 = clampd(-filter_ / off, 0, 1);
    const double wHp0 = wHp_;
    const double wHp1 = clampd(filter_ / off, 0, 1);
    wLp_ = wLp1;
    wHp_ = wHp1;

    // SPREAD x: до 0.5 — разнос времени o = x, без перекрёстной связи; дальше c 0 → 1, o = 0.5·(1 − c).
    const double c0 = sp0 > 0.5 ? (sp0 - 0.5) / 0.5 : 0;
    const double c1 = sp1 > 0.5 ? (sp1 - 0.5) / 0.5 : 0;
    const double o1 = sp1 > 0.5 ? 0.5 * (1 - c1) : sp1;
    const double targL = tTime * sr;
    const double targR = tTime * (1 + o1) * sr;
    if (dL_ == 0) {
        dL_ = targL;
        dR_ = targR;
    }
    const double g = 1 - std::exp(-1 / ((kGlideMs / 1000) * sr));
    const double gm = kGlideMax;

    float* const lineL = lineL_;
    float* const lineR = lineR_;
    const int mask = mask_, len = len_;
    const double k0 = kHp_[0], k1 = kHp_[1], k2 = kHp_[2], k3 = kHp_[3], k4 = kHp_[4];
    double hx1L = zHp_[0], hx2L = zHp_[1], hy1L = zHp_[2], hy2L = zHp_[3];
    double hx1R = zHp_[4], hx2R = zHp_[5], hy1R = zHp_[6], hy2R = zHp_[7];
    const double la1 = cLp_[0], la2 = cLp_[1], la3 = cLp_[2];
    const double ha1 = cHp_[0], ha2 = cHp_[1], ha3 = cHp_[2];
    double lp1L = sLp_[0], lp2L = sLp_[1], lp1R = sLp_[2], lp2R = sLp_[3];
    double hp1L = sHp_[0], hp2L = sHp_[1], hp1R = sHp_[2], hp2R = sHp_[3];
    const double k = kSatK;
    const double wowAmp = (kWowMs / 1000) * sr;
    const double flAmp = (kFlutterMs / 1000) * sr;
    const double wowStep = kWowHz / sr;
    double wp0 = wowPh_[0], wp1 = wowPh_[1];
    double wa0 = wowA_[0], wa1 = wowA_[1];
    double wb0 = wowB_[0], wb1 = wowB_[1];
    double fs0 = flS_[0], fs1 = flS_[1];
    double fc0 = flC_[0], fc1 = flC_[1];
    const double flR = flR_, flI = flI_;
    const double maxD = double(len - 4);
    const double sqrt2 = 1.4142135623730951;
    const double nD = double(n);
    int w = w_;
    double dL = dL_, dR = dR_;
    bool bad = false;

    for (int s = 0; s < n; ++s) {
        const double f = double(s + 1) / nD;
        const double fbS = fb0 + (fb1 - fb0) * f;
        const double c = c0 + (c1 - c0) * f;
        double st = (targL - dL) * g;
        dL += st > gm ? gm : (st < -gm ? -gm : st);
        st = (targR - dR) * g;
        dR += st > gm ? gm : (st < -gm ? -gm : st);
        // Модуляция линий: wow (случайное блуждание) + flutter (синус).
        double ph = wp0 + wowStep;
        if (ph >= 1) { ph -= 1; wa0 = wb0; wb0 = rng_.next() * 2 - 1; }
        wp0 = ph;
        double sn = fs0 * flR + fc0 * flI;
        fc0 = fc0 * flR - fs0 * flI;
        fs0 = sn;
        ph = wp1 + wowStep;
        if (ph >= 1) { ph -= 1; wa1 = wb1; wb1 = rng_.next() * 2 - 1; }
        wp1 = ph;
        sn = fs1 * flR + fc1 * flI;
        fc1 = fc1 * flR - fs1 * flI;
        fs1 = sn;
        const double u0 = 0.5 - 0.5 * std::cos(kPi * wp0);
        const double u1 = 0.5 - 0.5 * std::cos(kPi * wp1);
        const double mod0 = (wa0 + (wb0 - wa0) * u0) * wowAmp + fs0 * flAmp;
        const double mod1 = (wa1 + (wb1 - wa1) * u1) * wowAmp + fs1 * flAmp;
        // Чтение линий (кубический Эрмит).
        double yL, yR;
        {
            const double d = std::min(maxD, std::max(1.0, dL + mod0));
            const double pos = double(w) - d + double(len);
            const int ip = int(pos);  // позиция положительна
            const double fr = pos - double(ip);
            const double y0 = lineL[(ip - 1) & mask], y1 = lineL[ip & mask];
            const double y2 = lineL[(ip + 1) & mask], y3 = lineL[(ip + 2) & mask];
            const double h1 = 0.5 * (y2 - y0);
            const double h2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
            const double h3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            yL = ((h3 * fr + h2) * fr + h1) * fr + y1;
        }
        {
            const double d = std::min(maxD, std::max(1.0, dR + mod1));
            const double pos = double(w) - d + double(len);
            const int ip = int(pos);
            const double fr = pos - double(ip);
            const double y0 = lineR[(ip - 1) & mask], y1 = lineR[ip & mask];
            const double y2 = lineR[(ip + 1) & mask], y3 = lineR[(ip + 2) & mask];
            const double h1 = 0.5 * (y2 - y0);
            const double h2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
            const double h3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            yR = ((h3 * fr + h2) * fr + h1) * fr + y1;
        }
        // Выход линий: HP 60 Гц (прямая форма I) → FILTER.
        double y = k0 * yL + k1 * hx1L + k2 * hx2L - k3 * hy1L - k4 * hy2L;
        hx2L = hx1L; hx1L = yL; hy2L = hy1L; hy1L = y;
        yL = y;
        y = k0 * yR + k1 * hx1R + k2 * hx2R - k3 * hy1R - k4 * hy2R;
        hx2R = hx1R; hx1R = yR; hy2R = hy1R; hy1R = y;
        yR = y;
        {
            const double wl = wLp0 + (wLp1 - wLp0) * f;
            const double wh = wHp0 + (wHp1 - wHp0) * f;
            // LP (SVF) L.
            double v3 = yL - lp2L;
            double v1 = la1 * lp1L + la2 * v3;
            double v2 = lp2L + la2 * lp1L + la3 * v3;
            lp1L = 2 * v1 - lp1L; lp2L = 2 * v2 - lp2L;
            yL += (v2 - yL) * wl;
            // HP (SVF) L.
            v3 = yL - hp2L;
            v1 = ha1 * hp1L + ha2 * v3;
            v2 = hp2L + ha2 * hp1L + ha3 * v3;
            hp1L = 2 * v1 - hp1L; hp2L = 2 * v2 - hp2L;
            double v = yL - sqrt2 * v1 - v2;
            yL += (v - yL) * wh;
            // LP R.
            v3 = yR - lp2R;
            v1 = la1 * lp1R + la2 * v3;
            v2 = lp2R + la2 * lp1R + la3 * v3;
            lp1R = 2 * v1 - lp1R; lp2R = 2 * v2 - lp2R;
            yR += (v2 - yR) * wl;
            // HP R.
            v3 = yR - hp2R;
            v1 = ha1 * hp1R + ha2 * v3;
            v2 = hp2R + ha2 * hp1R + ha3 * v3;
            hp1R = 2 * v1 - hp1R; hp2R = 2 * v2 - hp2R;
            v = yR - sqrt2 * v1 - v2;
            yR += (v - yR) * wh;
        }
        // Обратная связь: мягкая сатурация, перекрёстная связь c.
        const double sL = std::tanh(k * yL) / k;
        const double sR = std::tanh(k * yR) / k;
        // Вход: при c → 1 эхо входит только в левую линию (моно-сумма).
        const double xl = inL ? double(inL[s]) : 0.0;
        const double xr = inR ? double(inR[s]) : 0.0;
        const double m = (xl + xr) * 0.5;
        lineL[w] = float((1 - c) * xl + c * m + fbS * ((1 - c) * sL + c * sR) + kTiny);
        lineR[w] = float((1 - c) * xr + fbS * ((1 - c) * sR + c * sL) + kTiny);
        w = (w + 1) & mask;
        outL[s] = float(sL);
        outR[s] = float(sR);
        if (!std::isfinite(yL) || !std::isfinite(yR) || yL > 1e6 || yL < -1e6 || yR > 1e6 || yR < -1e6) bad = true;
    }
    w_ = w;
    dL_ = dL;
    dR_ = dR;
    wowPh_[0] = wp0; wowPh_[1] = wp1;
    wowA_[0] = wa0; wowA_[1] = wa1;
    wowB_[0] = wb0; wowB_[1] = wb1;
    flS_[0] = fs0; flS_[1] = fs1;
    flC_[0] = fc0; flC_[1] = fc1;
    // Денормали в состояниях фильтров.
    const double z[8] = {hx1L, hx2L, hy1L, hy2L, hx1R, hx2R, hy1R, hy2R};
    for (int i = 0; i < 8; ++i) zHp_[i] = dn(z[i]);
    sLp_[0] = dn(lp1L); sLp_[1] = dn(lp2L); sLp_[2] = dn(lp1R); sLp_[3] = dn(lp2R);
    sHp_[0] = dn(hp1L); sHp_[1] = dn(hp2L); sHp_[2] = dn(hp1R); sHp_[3] = dn(hp2R);
    // NaN или разнос — сброс состояния (тишина вместо шума).
    if (bad) {
        reset();
        std::memset(outL, 0, sizeof(float) * n);
        std::memset(outR, 0, sizeof(float) * n);
    }
}

}  // namespace jambo
