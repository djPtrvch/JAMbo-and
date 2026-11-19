#include "Drive.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>

namespace jambo {

namespace {
constexpr double kSmoothMs = 20;
constexpr double kTypeFadeMs = 20;
constexpr double kDcHz = 5;
constexpr double kLowCutMin = 20;
constexpr double kLowCutMax = 1000;
constexpr double kLowCutOff = 25;
constexpr double kToneHz = 500;
constexpr double kToneRange = 24;
constexpr double kCrushBitsMax = 12;
constexpr double kCrushBitsMin = 3;
constexpr double kCrushHoldMax = 16;
constexpr double kTiny = 1e-18;
constexpr double kIdleMs = 200;
constexpr double kIdleLevel = 1e-7;
constexpr double kAdaaEps = 1e-6;
/// Задержка цепи оверсэмплинга, исходных отсчётов: (64 + 0,5 + 3,5 + 64) / 4.
constexpr int kOsDelay = 33;
constexpr int kCorrLen = 8;
constexpr double kPi = 3.14159265358979323846;

/// RBJ (Q = 1/√2) LP или HP → [b0, b1, b2, a1, a2].
void rbj(double* out, bool hp, double f, double sr) {
    const double w = 2 * kPi * std::min(f, sr * 0.45) / sr;
    const double cw = std::cos(w);
    const double al = std::sin(w) / 1.4142135623730951;
    const double a0 = 1 + al;
    const double b0 = (hp ? (1 + cw) / 2 : (1 - cw) / 2) / a0;
    out[0] = b0;
    out[1] = (hp ? -(1 + cw) : 1 - cw) / a0;
    out[2] = b0;
    out[3] = (-2 * cw) / a0;
    out[4] = (1 - al) / a0;
}

/// Округление: половина — вверх.
inline double jsRound(double x) { return std::floor(x + 0.5); }

/// Свёртка n отводов: acc + Σ k[j]·x[−j] (x — последний отсчёт окна); порядок сложения как в Swift.
inline double dot(double acc, const double* k, const double* x, int n) {
    double s = acc;
    for (int j = 0; j < n; ++j) s += k[j] * x[-j];
    return s;
}

/// Симметричная свёртка: acc + Σ k[j]·(a[−j] + b[j]).
inline double dotSym(double acc, const double* k, const double* a, const double* b, int n) {
    double s = acc;
    for (int j = 0; j < n; ++j) s += k[j] * (a[-j] + b[j]);
    return s;
}

/// Кривая type 0–3.
inline double shapeY(int type, double u, double tb, double td) {
    if (type == 0) return std::tanh(u);
    if (type == 1) return u > 1 ? 1 : (u < -1 ? -1 : u);
    if (type == 2) return (std::tanh(u + 0.35) - tb) / td;
    return u < 0 ? -(1 - std::exp(u)) : 1 - std::exp(-u);
}

/// Антипроизводная type 0–3 (F, D, fmax — таблица кривой: tanh у 0 и 2, fuzz у 3).
inline double antiF(int type, double u, const double* F, const double* D, double fmax, double lcB, double tb, double td) {
    if (type == 1) {
        const double a = u < 0 ? -u : u;
        return a <= 1 ? 0.5 * u * u : a - 0.5;
    }
    if (type == 2) return (DriveTables::tableF(F, D, fmax, u + 0.35) - lcB - u * tb) / td;
    return DriveTables::tableF(F, D, fmax, u);
}
}  // namespace

FxDrive::FxDrive(double sampleRate, const DriveTables& tables)
    : sr_(sampleRate), t_(tables),
      lcSig_(std::numeric_limits<double>::quiet_NaN()), toneSig_(std::numeric_limits<double>::quiet_NaN()) {
    kSmooth_ = std::exp(-1 / ((kSmoothMs / 1000) * sampleRate));
    fadeStep_ = 1 / ((kTypeFadeMs / 1000) * sampleRate);
    kDc_ = std::exp((-2 * kPi * kDcHz) / sampleRate);
}

void FxDrive::reset() {
    std::memset(st_, 0, sizeof st_);
    std::memset(ist_, 0, sizeof ist_);
}

double FxDrive::anti(int type, double u) const {
    switch (type) {
    case 0: return DriveTables::tableF(t_.tanhF.data(), t_.tanhD.data(), t_.tanhMax, u);
    case 1: {
        const double a = u < 0 ? -u : u;
        return a <= 1 ? 0.5 * u * u : a - 0.5;
    }
    case 2: return (DriveTables::tableF(t_.tanhF.data(), t_.tanhD.data(), t_.tanhMax, u + 0.35) - t_.lcB - u * t_.tb) / t_.td;
    default: return DriveTables::tableF(t_.fuzzF.data(), t_.fuzzD.data(), t_.fuzzMax, u);
    }
}

/// Нелинейность type по 4 отсчётам u: y[p] += k · кривая (c — состояние канала).
void FxDrive::shape(double* c, int type, double k) const {
    double* u = c + oU;
    double* y = c + oY;
    double u0 = c[oPrevU];
    if (type == fold) {
        double s0 = c[oPrevS];
        for (int p = 0; p < 4; ++p) {
            const double s = std::sin(u[p]);
            y[p] += k * 0.5 * (s + s0);
            s0 = s;
        }
        c[oPrevS] = s0;
        return;
    }
    double F0 = c[oPrevF + type];
    const bool fz = type == 3;
    const double* tF = fz ? t_.fuzzF.data() : t_.tanhF.data();
    const double* tD = fz ? t_.fuzzD.data() : t_.tanhD.data();
    const double tMax = fz ? t_.fuzzMax : t_.tanhMax;
    const double lcB = t_.lcB, tb = t_.tb, td = t_.td;
    for (int p = 0; p < 4; ++p) {
        const double v = u[p];
        const double F = antiF(type, v, tF, tD, tMax, lcB, tb, td);
        const double du = v - u0;
        const bool near = du < kAdaaEps && du > -kAdaaEps;
        y[p] += k * (near ? shapeY(type, 0.5 * (v + u0), tb, td) : (F - F0) / du);
        u0 = v;
        F0 = F;
    }
    c[oPrevF + type] = F0;
}

/// Отсчёт (уже · g) → 4 отсчёта нелинейности (смесь типов a и b, только 0–4) → децимация.
double FxDrive::wet(double* c, int* ic, double x, int ta, int tb, double wa, double wb, double ka, double kb) const {
    const int pl = DriveTables::phaseLen;
    double* hist = c + oHist;
    const int hp = ic[0];
    hist[hp] = x;
    hist[hp + pl] = x;
    ic[0] = hp + 1 == pl ? 0 : hp + 1;
    const int base = hp + pl;
    double* u = c + oU;
    u[0] = t_.ph0Gain * hist[base - DriveTables::ph0Delay];
    const double* phases = t_.phases.data();
    for (int p = 1; p < 4; ++p) u[p] = dot(0, phases + p * pl, hist + base, pl);
    double* y = c + oY;
    y[0] = 0; y[1] = 0; y[2] = 0; y[3] = 0;
    if (wa > 0 && ta != crush) shape(c, ta, wa / ka);
    if (wb > 0 && tb != crush) shape(c, tb, wb / kb);
    c[oPrevU] = u[3];
    // Коррекция среднего (1 − sin⁸) и децимация.
    double* cb = c + oCorr;
    double* os = c + oOs;
    const int cl = kCorrLen, fl = DriveTables::firLen;
    const double k0 = -1.0 / 128, k1 = 9.0 / 128, k2 = -37.0 / 128, k3 = 93.0 / 128;
    int cp = ic[1], op = ic[2];
    for (int p = 0; p < 4; ++p) {
        cb[cp] = y[p];
        cb[cp + cl] = y[p];
        const int e = cp + cl;
        cp = cp + 1 == cl ? 0 : cp + 1;
        const double yc = k0 * (cb[e] + cb[e - 7]) + k1 * (cb[e - 1] + cb[e - 6]) + k2 * (cb[e - 2] + cb[e - 5])
            + k3 * (cb[e - 3] + cb[e - 4]);
        os[op] = yc;
        os[op + fl] = yc;
        op = op + 1 == fl ? 0 : op + 1;
    }
    ic[1] = cp;
    ic[2] = op;
    const int ob = op + fl - 1;
    const int dc = DriveTables::decC;
    double out = t_.decMid * os[ob - dc];
    const int lst = ob - (fl - 1);
    return dotSym(out, t_.decHalf.data(), os + ob, os + lst, dc);
}

void FxDrive::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                      float dIn, float tIn, float toIn, float lIn) {
    const double sr = sr_;
    const double driveT = std::max(0.0, std::min(100.0, double(dIn)));
    const double toneT = std::max(0.0, std::min(100.0, double(toIn)));
    const double lcT = std::max(kLowCutMin, std::min(kLowCutMax, double(lIn)));
    const int type = int(std::max(0.0, std::min(5.0, jsRound(double(tIn)))));
    if (!started_) {
        started_ = true;
        drive_ = driveT; tone_ = toneT; lowcut_ = lcT; cur_ = type; prev_ = type;
        lcW_ = lcT > kLowCutOff ? 1 : 0;
    }
    if (type != cur_) {
        // Смена TYPE: кроссфейд 20 мс; новая кривая начинает антипроизводную с текущего отсчёта.
        prev_ = fade_ >= 0.5 ? cur_ : prev_;
        cur_ = type;
        fade_ = 0;
        for (int k = 0; k < 2; ++k) {
            double* c = st_ + k * chSize;
            if (type < 4) c[oPrevF + type] = anti(type, c[oPrevU]);
            if (type == fold) c[oPrevS] = std::sin(c[oPrevU]);
        }
    }
    // TONE и LOW CUT — по блокам (сглаживание за 20 мс), коэффициенты — при изменении.
    const double kb = std::pow(kSmooth_, double(n));
    tone_ = toneT + (tone_ - toneT) * kb;
    if (std::fabs(tone_ - toneT) < 1e-3) tone_ = toneT;
    lowcut_ = std::exp(std::log(lcT) + (std::log(lowcut_) - std::log(lcT)) * kb);
    if (std::fabs(lowcut_ - lcT) < 1e-3) lowcut_ = lcT;
    if (tone_ != toneSig_) {
        toneSig_ = tone_;
        rbj(cTone_, false, kToneHz * std::pow(kToneRange, tone_ / 100), sr);
    }
    if (lowcut_ != lcSig_) {
        lcSig_ = lowcut_;
        rbj(cLc_, true, std::max(kLowCutMin, lowcut_), sr);
    }
    // LOW CUT ≤ 25 Гц — обход: доля фильтра плавно 0 ↔ 1.
    const double lcW0 = lcW_;
    const double lcW1 = lowcut_ > kLowCutOff ? 1 : 0;
    lcW_ = lcW0 + (lcW1 - lcW0) * (1 - kb);
    if (std::fabs(lcW_ - lcW1) < 1e-4) lcW_ = lcW1;

    // Покой: на входе тишина дольше 200 мс — выход нули.
    double peak = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const float* a = ch == 0 ? inL : inR;
        if (!a) continue;
        for (int i = 0; i < n; ++i) {
            const double v = std::fabs(double(a[i]));
            if (v > peak) peak = v;
        }
    }
    silent_ = peak < kIdleLevel ? silent_ + double(n) : 0;
    if (silent_ > (kIdleMs / 1000) * sr) {
        std::memset(outL, 0, sizeof(float) * n);
        std::memset(outR, 0, sizeof(float) * n);
        drive_ = driveT;
        if (fade_ < 1) fade_ = 1;
        return;
    }
    const double ks = kSmooth_, kDc = kDc_;
    const double* comp = t_.comp.data();
    const int cn = DriveTables::compN + 2;
    const int pl = DriveTables::phaseLen;
    const double lcWEnd = lcW_;
    bool bad = false;
    for (int i = 0; i < n; ++i) {
        drive_ = driveT + (drive_ - driveT) * ks;
        if (fade_ < 1) fade_ = std::min(1.0, fade_ + fadeStep_);
        const double D = drive_ / 100;
        const double dB = 36 * D;
        const double g = std::exp(dB * 0.11512925464970229);
        const double ci = dB / 0.1;
        const double i0f = std::floor(ci);
        const int i0 = int(i0f);
        const double fr = ci - i0f;
        const int ta = prev_, tb = cur_;
        const double wb = fade_;
        const double wa = ta == tb ? 0 : 1 - wb;
        const double ka = ta == crush ? 1 : comp[ta * cn + i0] + (comp[ta * cn + i0 + 1] - comp[ta * cn + i0]) * fr;
        const double kB = tb == crush ? 1 : comp[tb * cn + i0] + (comp[tb * cn + i0 + 1] - comp[tb * cn + i0]) * fr;
        const bool osNeeded = (wa > 0 && ta != crush) || (wb > 0 && tb != crush);
        const double crW = (ta == crush ? wa : 0) + (tb == crush ? wb : 0);
        // CRUSH: шаг квантования 2^(1 − B), B = 12 − 9·D; повтор N = 1 + round(15·D²).
        const double step = std::pow(2.0, 1 - (kCrushBitsMax - (kCrushBitsMax - kCrushBitsMin) * D));
        const int holdN = 1 + int(jsRound((kCrushHoldMax - 1) * D * D));
        const double lcw = lcW0 + (lcWEnd - lcW0) * (double(i + 1) / double(n));
        for (int k = 0; k < 2; ++k) {
            double* c = st_ + k * chSize;
            int* ic = ist_ + k * iSize;
            const float* in = k == 0 ? inL : inR;
            double x = in ? double(in[i]) : 0.0;
            // LOW CUT (до искажения), TDF-II.
            double* lc = c + oLc;
            const double xl = cLc_[0] * x + lc[0];
            lc[0] = cLc_[1] * x - cLc_[3] * xl + lc[1];
            lc[1] = cLc_[2] * x - cLc_[4] * xl;
            x += (xl - x) * lcw;
            double y = 0;
            if (osNeeded) {
                y = wet(c, ic, x * g, ta, tb, wa, wb, ka, kB);
            } else {
                // История оверсэмплера идёт дальше — вход в него потом без скачка.
                const int hp = ic[0];
                c[oHist + hp] = x * g;
                c[oHist + hp + pl] = x * g;
                ic[0] = hp + 1 == pl ? 0 : hp + 1;
            }
            // CRUSH — на входе, задержанном на задержку оверсэмплинга.
            const int dp = ic[3];
            const double xd = c[oCd + dp];
            c[oCd + dp] = x;
            ic[3] = dp + 1 == kOsDelay ? 0 : dp + 1;
            if (crW > 0) {
                if (ic[4] <= 0) {
                    double q = step * jsRound(xd / step);
                    if (q > 1 - step) q = 1 - step; else if (q < -1) q = -1;
                    c[oHold] = q;
                    ic[4] = holdN;
                }
                ic[4] -= 1;
                y += crW * c[oHold];
            } else {
                ic[4] = 0;
            }
            // Фильтр DC (HP 1-го порядка 5 Гц), TONE (TDF-II).
            const double hpY = kDc * (c[oDcY] + y - c[oDcX]) + kTiny;
            c[oDcX] = y;
            c[oDcY] = hpY;
            double* tn = c + oTone;
            y = cTone_[0] * hpY + tn[0];
            tn[0] = cTone_[1] * hpY - cTone_[3] * y + tn[1];
            tn[1] = cTone_[2] * hpY - cTone_[4] * y;
            if (k == 0) outL[i] = float(y); else outR[i] = float(y);
            if (!std::isfinite(y) || std::fabs(y) > 1e6) bad = true;
        }
    }
    if (bad) {
        // NaN или разнос — тишина и сброс состояний.
        reset();
        std::memset(outL, 0, sizeof(float) * n);
        std::memset(outR, 0, sizeof(float) * n);
    }
}

}  // namespace jambo
