#include <cstdint>
#include "Reverb.h"
#include "FxRandom.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>

namespace jambo {

namespace {
// Константы (REVERB_CONST).
constexpr double kDiffusion = 0.7;
constexpr double kModulation = 0.3;
constexpr double kModDepthMax = 20;
constexpr double kModRates[8] = {0.1, 0.21, 0.33, 0.44, 0.56, 0.67, 0.79, 0.9};
constexpr double kEarly = 0.4;
constexpr double kHighCutHz = 14000;
constexpr double kReturnDb = 0;
constexpr double kFdnLengths[8] = {1123, 1361, 1597, 1823, 2063, 2297, 2539, 2803};
constexpr double kDiffLensL[4] = {229, 173, 611, 447};
constexpr double kDiffLensR[4] = {241, 181, 593, 461};
constexpr double kDiffCoefs[4] = {0.75, 0.75, 0.625, 0.625};
constexpr double kErTimesL[12] = {3.1, 5.3, 7.9, 10.7, 13.3, 16.9, 19.7, 23.3, 27.1, 31.3, 35.9, 41.0};
constexpr double kErTimesR[12] = {3.7, 4.9, 8.6, 11.3, 14.1, 15.8, 20.6, 24.2, 26.3, 32.8, 34.7, 43.1};
constexpr double kPreBaseMs = 4;
constexpr double kPrePerSize = 0.22;
constexpr double kScaleMin = 0.35;
constexpr double kScaleRange = 1.65;
constexpr double kToneLo = 0.12;
constexpr double kToneMid = 0.5;
constexpr double kToneHi = 1;
constexpr double kDampHz = 8000;
constexpr double kShelfHz = 6000;
constexpr double kShelfDb = 4;
constexpr double kSmoothMs = 50;
constexpr double kSizeGlideMs = 150;
constexpr double kLateGain = 0.35;
constexpr double kTiny = 1e-18;
constexpr double kMaxPreMs = 240;
constexpr double kPi = 3.14159265358979323846;

int pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

/// Биквад RBJ (Q = 1/√2): LP или HP.
void biquad(double* out, bool lp, double f, double sr) {
    const double w = 2 * kPi * std::min(f, sr * 0.45) / sr;
    const double cw = std::cos(w);
    const double al = std::sin(w) / (2 * 0.7071067811865476);
    const double a0 = 1 + al;
    const double b0 = lp ? (1 - cw) / 2 : (1 + cw) / 2;
    const double b1 = lp ? 1 - cw : -(1 + cw);
    out[0] = b0 / a0; out[1] = b1 / a0; out[2] = b0 / a0; out[3] = (-2 * cw) / a0; out[4] = (1 - al) / a0;
}

/// Полка (high shelf, S = 1).
void shelf(double* out, double f, double sr, double gainDb) {
    const double w = 2 * kPi * std::min(f, sr * 0.45) / sr;
    const double cw = std::cos(w);
    const double sw = std::sin(w);
    const double A = std::pow(10.0, gainDb / 40);
    const double al = (sw / 2) * 1.4142135623730951;
    const double s = 2 * std::sqrt(A) * al;
    const double b0 = A * ((A + 1) + (A - 1) * cw + s);
    const double b1 = -2 * A * ((A - 1) + (A + 1) * cw);
    const double b2 = A * ((A + 1) + (A - 1) * cw - s);
    const double a0 = (A + 1) - (A - 1) * cw + s;
    const double a1 = 2 * ((A - 1) - (A + 1) * cw);
    const double a2 = (A + 1) - (A - 1) * cw - s;
    out[0] = b0 / a0; out[1] = b1 / a0; out[2] = b2 / a0; out[3] = a1 / a0; out[4] = a2 / a0;
}

/// Округление: половина — вверх.
inline double jsRound(double x) { return std::floor(x + 0.5); }
}  // namespace

FxReverb::FxReverb(double sampleRate, uint32_t seed)
    : sr_(sampleRate), preMs_(std::numeric_limits<double>::quiet_NaN()),
      coefSig0_(std::numeric_limits<double>::quiet_NaN()), coefSig1_(std::numeric_limits<double>::quiet_NaN()) {
    const double k = sampleRate / 48000;
    scale_ = kScaleMin + kScaleRange * 0.5;
    biquad(kLp_, true, kHighCutHz, sampleRate);
    preLen_ = pow2(int(std::ceil(kMaxPreMs / 1000 * sampleRate)) + 8);
    preL_.assign(preLen_, 0);
    preR_.assign(preLen_, 0);
    erLen_ = pow2(int(std::ceil(0.1 * sampleRate)) + 8);
    erL_.assign(erLen_, 0);
    erR_.assign(erLen_, 0);
    // Отводы: знаки чередуются, усиления спадают; сумма модулей — 1 (Float32).
    double g[12];
    double sum = 0;
    for (int i = 0; i < 12; ++i) {
        g[i] = std::pow(0.85, double(i)) * (i % 2 == 1 ? -1 : 1);
        sum += std::fabs(g[i]);
    }
    for (int i = 0; i < 12; ++i) {
        erGain_[i] = double(float(g[i] / sum));
        erTL_[i] = kErTimesL[i];
        erTR_[i] = kErTimesR[i];
    }
    // All-pass: длина l·k, буфер — степень двойки больше длины.
    int off = 0;
    for (int i = 0; i < 8; ++i) {
        const double l = i < 4 ? kDiffLensL[i] : kDiffLensR[i - 4];
        const int len = int(std::round(l * k));
        const int size = pow2(len + 1);
        apOff_[i] = off;
        apMask_[i] = size - 1;
        apLen_[i] = len;
        apW_[i] = 0;
        off += size;
    }
    apBuf_.assign(off, 0);
    for (int i = 0; i < 4; ++i) apG_[i] = double(float(std::min(0.95, (kDiffCoefs[i] * kDiffusion) / 0.7)));
    // FDN: линии с запасом на SIZE 100 % и модуляцию.
    const double maxLen = 2803 * (kScaleMin + kScaleRange) * k + kModDepthMax * k + 16;
    lineSize_ = pow2(int(std::ceil(maxLen)));
    mask_ = lineSize_ - 1;
    lines_.assign(size_t(lineSize_) * 8, 0);
    FxRandom rng(seed);
    for (int i = 0; i < 8; ++i) {
        base_[i] = kFdnLengths[i] * k;
        gain_[i] = 0; damp_[i] = 0; lp_[i] = 0; rd_[i] = 0; v_[i] = 0;
        const double ph = rng.next() * 2 * kPi;
        ms_[i] = std::sin(ph);
        mc_[i] = std::cos(ph);
        const double wl = 2 * kPi * kModRates[i] / sampleRate;
        mr_[i] = std::cos(wl);
        mi_[i] = std::sin(wl);
    }
    modDepth_ = kModulation * kModDepthMax * k;
    ret_ = std::pow(10.0, kReturnDb / 20);
}

double FxReverb::scaleOf(double size) const { return kScaleMin + kScaleRange * (size / 100); }

double FxReverb::toneR(double t) const {
    const double u = std::max(-1.0, std::min(1.0, t / 100));
    const double lo = std::log(kToneLo), mid = std::log(kToneMid), hi = std::log(kToneHi);
    return u < 0 ? std::exp(mid + (mid - lo) * u) : std::exp(mid + (hi - mid) * u);
}

void FxReverb::updateLoop() {
    const double r = toneR(tone_);
    const double c = std::cos(2 * kPi * kDampHz / sr_);
    for (int i = 0; i < 8; ++i) {
        const double L = base_[i] * scale_;
        const double g = std::pow(10.0, (-3 * L) / (sr_ * decay_));
        const double gHf = std::pow(10.0, (-3 * L) / (sr_ * decay_ * r));
        gain_[i] = std::min(0.9995, g);
        const double m = std::min(1.0, gHf / g);
        const double k2 = std::pow(m, 2);
        if (k2 >= 0.999999) { damp_[i] = 0; continue; }
        const double p = 1 - k2 * c;
        const double q = 1 - k2;
        damp_[i] = std::max(0.0, std::min(0.999, (p - std::sqrt(std::max(0.0, p * p - q * q))) / q));
    }
}

void FxReverb::updateFilters() {
    const double s0 = jsRound(lowcut_ * 10), s1 = jsRound(tone_ * 10);
    if (s0 == coefSig0_ && s1 == coefSig1_) return;
    coefSig0_ = s0;
    coefSig1_ = s1;
    biquad(kHp_, false, lowcut_, sr_);
    shelf(kSh_, kShelfHz, sr_, (kShelfDb * tone_) / 100);
}

void FxReverb::sleep() {
    reset();
    started_ = false;
    preMs_ = std::numeric_limits<double>::quiet_NaN();
}

void FxReverb::reset() {
    std::fill(preL_.begin(), preL_.end(), 0.0f);
    std::fill(preR_.begin(), preR_.end(), 0.0f);
    std::fill(erL_.begin(), erL_.end(), 0.0f);
    std::fill(erR_.begin(), erR_.end(), 0.0f);
    std::fill(lines_.begin(), lines_.end(), 0.0f);
    std::fill(apBuf_.begin(), apBuf_.end(), 0.0f);
    std::memset(zHp_, 0, sizeof zHp_);
    std::memset(zLp_, 0, sizeof zLp_);
    std::memset(zSh_, 0, sizeof zSh_);
    std::memset(lp_, 0, sizeof lp_);
}

void FxReverb::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                       float sIn, float dIn, float tIn, float lIn, float pIn) {
    const double sr = sr_;
    const double tSize = sIn, tDecay = dIn, tTone = tIn, tLow = lIn, tPre = pIn;
    if (!started_) {
        started_ = true;
        size_ = tSize; decay_ = tDecay; tone_ = tTone; lowcut_ = tLow;
        scale_ = scaleOf(tSize);
    }
    const double a = 1 - std::exp((-3 * double(n)) / ((kSmoothMs / 1000) * sr));
    decay_ += (tDecay - decay_) * a;
    tone_ += (tTone - tone_) * a;
    lowcut_ = std::exp(std::log(lowcut_) + (std::log(tLow) - std::log(lowcut_)) * a);
    const double preT = kPreBaseMs + kPrePerSize * tSize + tPre;
    const double pre0 = std::isnan(preMs_) ? preT : preMs_;
    const double pre1 = pre0 + (preT - pre0) * a;
    preMs_ = pre1;
    const double scale0 = scale_;
    const double gs = 1 - std::exp((-3 * double(n)) / ((kSizeGlideMs / 1000) * sr));
    const double scale1 = scale0 + (scaleOf(tSize) - scale0) * gs;
    scale_ = scale1;
    updateFilters();
    updateLoop();

    float* const lines = lines_.data();
    const int mask = mask_, lineSize = lineSize_;
    float* const preL = preL_.data();
    float* const preR = preR_.data();
    const int preLen = preLen_, preMask = preLen - 1;
    float* const erL = erL_.data();
    float* const erR = erR_.data();
    const int erLen = erLen_, erMask = erLen - 1;
    float* const apBuf = apBuf_.data();
    const double depth = modDepth_, ret = ret_;
    const double nD = double(n);
    int w = w_, pw = preW_, ew = erW_;
    bool bad = false;

    for (int s = 0; s < n; ++s) {
        const double f = double(s + 1) / nD;
        const double scale = scale0 + (scale1 - scale0) * f;
        // Вход: LOW CUT → HIGH CUT (биквады, прямая форма I; L — состояния 0…3, R — 4…7).
        double xl = inL ? double(inL[s]) : 0.0;
        double xr = inR ? double(inR[s]) : 0.0;
        for (int o = 0; o < 8; o += 4) {
            const double x = o == 0 ? xl : xr;
            double y = kHp_[0] * x + kHp_[1] * zHp_[o] + kHp_[2] * zHp_[o + 1] - kHp_[3] * zHp_[o + 2] - kHp_[4] * zHp_[o + 3];
            zHp_[o + 1] = zHp_[o]; zHp_[o] = x; zHp_[o + 3] = zHp_[o + 2]; zHp_[o + 2] = y;
            const double x2 = y;
            y = kLp_[0] * x2 + kLp_[1] * zLp_[o] + kLp_[2] * zLp_[o + 1] - kLp_[3] * zLp_[o + 2] - kLp_[4] * zLp_[o + 3];
            zLp_[o + 1] = zLp_[o]; zLp_[o] = x2; zLp_[o + 3] = zLp_[o + 2]; zLp_[o + 2] = y;
            if (o == 0) xl = y; else xr = y;
        }
        // Пред-задержка (дробное чтение, линейно).
        preL[pw] = float(xl);
        preR[pw] = float(xr);
        const double pd = std::max(1.0, (pre1 / 1000) * sr);
        const double pp = double(pw) - pd + double(preLen * 2);
        const int pi = int(pp);  // позиция положительна
        const double pf = pp - double(pi);
        const double pl0 = preL[pi & preMask], pr0 = preR[pi & preMask];
        const double dl = pl0 + (double(preL[(pi + 1) & preMask]) - pl0) * pf;
        const double dr = pr0 + (double(preR[(pi + 1) & preMask]) - pr0) * pf;
        pw = (pw + 1) & preMask;
        // Ранние отражения: отводы после пред-задержки, времена × масштаб SIZE.
        erL[ew] = float(dl);
        erR[ew] = float(dr);
        double el = 0, er = 0;
        const double ems = (sr / 1000) * scale;
        for (int t = 0; t < 12; ++t) {
            const double ql = double(ew) - erTL_[t] * ems + double(erLen * 2);
            const int qi = int(ql);
            const double a0 = erL[qi & erMask];
            el += erGain_[t] * (a0 + (double(erL[(qi + 1) & erMask]) - a0) * (ql - double(qi)));
            const double qr = double(ew) - erTR_[t] * ems + double(erLen * 2);
            const int ri = int(qr);
            const double b0 = erR[ri & erMask];
            er += erGain_[t] * (b0 + (double(erR[(ri + 1) & erMask]) - b0) * (qr - double(ri)));
        }
        ew = (ew + 1) & erMask;
        // Входная диффузия: 4 all-pass на канал (L — 0…3, R — 4…7).
        double ul = dl, ur = dr;
        for (int kk = 0; kk < 4; ++kk) {
            const double g = apG_[kk];
            int j = kk;
            float* b = apBuf + apOff_[j];
            int ow = apW_[j];
            double ov = b[(ow - apLen_[j]) & apMask_[j]];
            double x = ul + g * ov;
            ul = ov - g * x;
            b[ow] = float(x + kTiny);
            apW_[j] = (ow + 1) & apMask_[j];
            j = kk + 4;
            b = apBuf + apOff_[j];
            ow = apW_[j];
            ov = b[(ow - apLen_[j]) & apMask_[j]];
            x = ur + g * ov;
            ur = ov - g * x;
            b[ow] = float(x + kTiny);
            apW_[j] = (ow + 1) & apMask_[j];
        }
        // FDN: чтение с модуляцией (кубический Эрмит), демпфирование, затухание.
        double sum = 0;
        for (int i = 0; i < 8; ++i) {
            const double sn = ms_[i] * mr_[i] + mc_[i] * mi_[i];
            mc_[i] = mc_[i] * mr_[i] - ms_[i] * mi_[i];
            ms_[i] = sn;
            const double d = base_[i] * scale + depth * sn;
            const double pos = double(w) - d + double(lineSize * 2);
            const int ip = int(pos);
            const double fr = pos - double(ip);
            const float* L = lines + size_t(i) * lineSize;
            const double y0 = L[(ip - 1) & mask], y1 = L[ip & mask];
            const double y2 = L[(ip + 1) & mask], y3 = L[(ip + 2) & mask];
            const double c1 = 0.5 * (y2 - y0);
            const double c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
            const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            const double val = ((c3 * fr + c2) * fr + c1) * fr + y1;
            rd_[i] = val;
            lp_[i] = val + damp_[i] * (lp_[i] - val);
            v_[i] = lp_[i] * gain_[i];
            sum += v_[i];
        }
        // Хаусхолдер: y = x − (2/8)·Σx; вход: L — в чётные линии, R — в нечётные.
        const double h = sum * 0.25;
        const double inLv = ul * 0.5, inRv = ur * 0.5;
        for (int i = 0; i < 8; ++i) {
            lines[size_t(i) * lineSize + w] = float(v_[i] - h + ((i & 1) == 1 ? inRv : inLv) + kTiny);
        }
        w = (w + 1) & mask;
        // Выход: хвост (знаки чередуются) + ранние отражения → полка TONE → возврат.
        double yl = (rd_[0] - rd_[2] + rd_[4] - rd_[6]) * kLateGain + el * kEarly;
        double yr = (rd_[1] - rd_[3] + rd_[5] - rd_[7]) * kLateGain + er * kEarly;
        double y = kSh_[0] * yl + kSh_[1] * zSh_[0] + kSh_[2] * zSh_[1] - kSh_[3] * zSh_[2] - kSh_[4] * zSh_[3];
        zSh_[1] = zSh_[0]; zSh_[0] = yl; zSh_[3] = zSh_[2]; zSh_[2] = y;
        yl = y;
        y = kSh_[0] * yr + kSh_[1] * zSh_[4] + kSh_[2] * zSh_[5] - kSh_[3] * zSh_[6] - kSh_[4] * zSh_[7];
        zSh_[5] = zSh_[4]; zSh_[4] = yr; zSh_[7] = zSh_[6]; zSh_[6] = y;
        yr = y;
        outL[s] = float(yl * ret);
        outR[s] = float(yr * ret);
        if (!std::isfinite(yl) || !std::isfinite(yr) || yl > 1e6 || yl < -1e6) bad = true;
    }
    w_ = w;
    preW_ = pw;
    erW_ = ew;
    // Денормали: состояния биквадов входа и TONE в тишине гаснут к нулю.
    for (int z = 0; z < 8; ++z) {
        if (zHp_[z] < 1e-30 && zHp_[z] > -1e-30) zHp_[z] = 0;
        if (zLp_[z] < 1e-30 && zLp_[z] > -1e-30) zLp_[z] = 0;
        if (zSh_[z] < 1e-30 && zSh_[z] > -1e-30) zSh_[z] = 0;
    }
    // NaN или разнос — сброс состояния (тишина вместо шума).
    if (bad) {
        reset();
        std::memset(outL, 0, sizeof(float) * n);
        std::memset(outR, 0, sizeof(float) * n);
    }
}

}  // namespace jambo
