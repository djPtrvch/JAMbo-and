// Компрессор мастера «glue» (порт Core/Master/Glue.swift).
#include "Glue.h"
#include <algorithm>

namespace jambo {

namespace {
inline double gabs(double x) { return x < 0 ? -x : x; }
}

MasterGlue::MasterGlue(double sampleRate)
    : sr(sampleRate), kSmooth(coef(smoothMs, sampleRate)), wetStep(1 / ((bypassMs / 1000) * sampleRate)),
      kPeak(coef(peakReleaseMs, sampleRate)), kRms(coef(rmsMs, sampleRate)), kAutoFast(coef(autoFastMs, sampleRate)),
      kAutoSlow(coef(autoSlowMs, sampleRate)) {
    updateHpf(90);
}

void MasterGlue::resetMeasure() {
    kSumIn = 0;
    kSumOut = 0;
    kCount = 0;
    std::fill(kf, kf + 16, 0.0);
}

void MasterGlue::updateHpf(double f) {
    if (f == hpfSig) return;
    hpfSig = f;
    const double w = (2 * M_PI * f) / sr;
    const double cw = std::cos(w);
    const double al = std::sin(w) / (2 * 0.7071067811865476);
    const double a0 = 1 + al;
    hb0 = (1 + cw) / 2 / a0;
    hb1 = -(1 + cw) / a0;
    hb2 = (1 + cw) / 2 / a0;
    ha1 = (-2 * cw) / a0;
    ha2 = (1 - al) / a0;
}

double MasterGlue::staticGr(double level, double thr, double ratio, double knee) {
    const double slope = 1 - 1 / std::max(1.0, std::min(20.0, ratio));
    const double k = std::max(0.0, std::min(24.0, knee));
    const double over = level - thr;
    const double half = k / 2;
    if (over >= half) return over * slope;
    if (over > -half) return (slope * (over + half) * (over + half)) / (2 * k);
    return 0;
}

double MasterGlue::nextRatio(double r) {
    for (double x : {2.0, 4.0, 10.0})
        if (x > r + 1e-3) return x;
    return 2;
}

void MasterGlue::process(const float* inL, const float* inR, float* outL, float* outR, int n, double thrT, double att,
                         double rel, bool autoRel, double mkT, double ratio, double kIn, double mIn0, double hpIn,
                         double bypass) {
    if (att != attSig) {
        attSig = att;
        kAtt = coef(att, sr);
    }
    if (rel != relSig) {
        relSig = rel;
        kRel = coef(rel, sr);
    }
    const double ks = kSmooth;
    const double slopeT = 1 - 1 / std::max(1.0, std::min(20.0, ratio));
    const double kneeT = std::max(0.0, std::min(24.0, kIn));
    const double mixT = std::max(0.0, std::min(1.0, mIn0 / 100));
    const double wetT = 1 - std::max(0.0, std::min(1.0, bypass));
    const double hpT = std::max(scHpfOff, std::min(500.0, hpIn));
    const double kb = std::pow(ks, double(n));
    schpf = std::exp(std::log(hpT) + (std::log(schpf) - std::log(hpT)) * kb);
    if (gabs(schpf - hpT) < 1e-3) schpf = hpT;
    const bool hpOn = schpf > scHpfOff + 1e-6;
    if (hpOn) updateHpf(schpf);
    const double b0 = hb0, b1 = hb1, b2 = hb2, a1 = ha1, a2 = ha2;
    const double kA = kAtt, kR = kRel, kAF = kAutoFast, kAS = kAutoSlow, kPk = kPeak, kRm = kRms;
    const bool meas = measuring;
    double sIn = 0, sOut = 0;
    for (int i = 0; i < n; ++i) {
        const double xl = double(inL[i]);
        const double xr = double(inR[i]);
        thr = thrT + (thr - thrT) * ks;
        makeupDb = mkT + (makeupDb - mkT) * ks;
        slope = slopeT + (slope - slopeT) * ks;
        knee = kneeT + (knee - kneeT) * ks;
        mix = mixT + (mix - mixT) * ks;
        if (wet < wetT)
            wet = std::min(wetT, wet + wetStep);
        else if (wet > wetT)
            wet = std::max(wetT, wet - wetStep);
        double hl = xl, hr = xr;
        if (hpOn) {
            hl = b0 * xl + b1 * zl1 + b2 * zl2 - a1 * zl3 - a2 * zl4 + tiny;
            zl2 = zl1;
            zl1 = xl;
            zl4 = zl3;
            zl3 = hl;
            hr = b0 * xr + b1 * zr1 + b2 * zr2 - a1 * zr3 - a2 * zr4 + tiny;
            zr2 = zr1;
            zr1 = xr;
            zr4 = zr3;
            zr3 = hr;
        }
        const double al = gabs(hl), ar = gabs(hr);
        const double a = al > ar ? al : ar;
        pk = a > pk ? a : pk * kPk + tiny;
        ms = a * a + (ms - a * a) * kRm + tiny;
        const double level = peakWeight * pk + rmsWeight * std::sqrt(ms) * rmsCal;
        ldb = 20 * std::log10(level + 1e-12);
        const double over = ldb - thr;
        const double half = knee / 2;
        double target = 0;
        if (over >= half)
            target = over * slope;
        else if (over > -half)
            target = (slope * (over + half) * (over + half)) / (2 * knee);
        if (target > env)
            env = target + (env - target) * kA;
        else if (autoRel)
            env = target + (env - target) * kAF;
        else
            env = target + (env - target) * kR;
        envSlow = autoRel ? target + (envSlow - target) * kAS : 0;
        const double gr = std::max(env, envSlow);
        const double g = std::pow(10.0, (makeupDb - gr) / 20);
        const double k = 1 - mix + g * mix;
        const double f = wet * k + (1 - wet);
        const double yl = xl * f, yr = xr * f;
        if (meas) {
            const double e = kWeigh(xl, 0) + kWeigh(xr, 8);
            const double f0 = wet * (1 - mix + std::pow(10.0, -gr / 20) * mix) + (1 - wet);
            sIn += e;
            sOut += e * f0 * f0;
        }
        outL[i] = float(yl);
        outR[i] = float(yr);
        const double ao = std::max(gabs(yl), gabs(yr));
        if (ao > mOut) mOut = ao;
        const double ai = std::max(gabs(xl), gabs(xr));
        if (ai > mIn) mIn = ai;
        if (gr * wet > mGr) mGr = gr * wet;
        if (ldb > mDet) mDet = ldb;
    }
    if (meas) {
        kSumIn += sIn;
        kSumOut += sOut;
        kCount += n;
    }
    meterN += n;
    if (meterN >= meterFrames) {
        frameIn = mIn;
        frameDet = mDet;
        frameGr = mGr;
        frameOut = mOut;
        ++frames;
        meterN = 0;
        mIn = 0;
        mGr = 0;
        mOut = 0;
        mDet = -240;
    }
}

double MasterGlue::kWeigh(double x, int o) {
    const double s0 = 1.53512485958697 * x - 2.69169618940638 * kf[o] + 1.19839281085285 * kf[o + 1] +
                      1.69065929318241 * kf[o + 2] - 0.73248077421585 * kf[o + 3] + tiny;
    kf[o + 1] = kf[o];
    kf[o] = x;
    kf[o + 3] = kf[o + 2];
    kf[o + 2] = s0;
    const double h = s0 - 2 * kf[o + 4] + kf[o + 5] + 1.99004745483398 * kf[o + 6] - 0.99007225036621 * kf[o + 7] + tiny;
    kf[o + 5] = kf[o + 4];
    kf[o + 4] = s0;
    kf[o + 7] = kf[o + 6];
    kf[o + 6] = h;
    return h * h;
}

}  // namespace jambo
