// Клиппер мастера (порт Core/Master/Clipper.swift).
#include "Clipper.h"
#include <algorithm>
#include <cmath>

namespace jambo {

namespace {
inline double cabs(double x) { return x < 0 ? -x : x; }
inline double clipDot(double acc, const double* k, const double* x, int n) {
    double s = acc;
    for (int j = 0; j < n; ++j) s += k[j] * x[-j];
    return s;
}
inline double clipDotSym(double acc, const double* k, const double* a, const double* b, int n) {
    double s = acc;
    for (int j = 0; j < n; ++j) s += k[j] * (a[-j] + b[j]);
    return s;
}
}  // namespace

MasterClipper::MasterClipper(double sampleRate, const DriveTables& tables)
    : sr(sampleRate), d(tables), st(size_t(2 * chSize), 0.0), ist(size_t(2 * iSize), 0) {
    kSmooth = std::exp(-1 / ((smoothMs / 1000) * sampleRate));
    kBlock = std::pow(kSmooth, 128);
    wetStep = 1 / ((bypassMs / 1000) * sampleRate);
    splitStep = 1 / ((smoothMs / 1000) * sampleRate);
    grWin = int((grWindowMs / 1000) * sampleRate + 0.5);
    knee = 1 - 0.6 * 0.3;
    ceil = std::pow(10.0, -1.0 / 20);
    butter(cf, 100, sampleRate, false);
    butter(cf + 5, 100, sampleRate, true);
}

void MasterClipper::butter(double* o, double f, double sr, bool high) {
    const double w = (2 * M_PI * f) / sr;
    const double cw = std::cos(w);
    const double al = std::sin(w) * 0.7071067811865476;
    const double a0 = 1 + al;
    const double b0 = (high ? (1 + cw) / 2 : (1 - cw) / 2) / a0;
    o[0] = b0;
    o[1] = (high ? -(1 + cw) : 1 - cw) / a0;
    o[2] = b0;
    o[3] = (-2 * cw) / a0;
    o[4] = (1 - al) / a0;
}

double MasterClipper::shape(double u, double k) {
    const double a = cabs(u);
    if (a <= k) return u;
    const double s = 1 - k;
    const double y = s > 1e-9 ? k + s * std::tanh((a - k) / s) : 1;
    return u < 0 ? -y : y;
}

double MasterClipper::antideriv(double u, double k) {
    const double a = cabs(u);
    if (a <= k) return 0.5 * u * u;
    const double s = 1 - k;
    const double base = 0.5 * k * k + k * (a - k);
    if (s <= 1e-9) return base + (a - k) * (1 - k);
    const double x = (a - k) / s;
    const double lc = x > 9 ? x - 0.6931471805599453 : x + std::log1p(std::exp(-2 * x)) - 0.6931471805599453;
    return base + s * s * lc;
}

double MasterClipper::hf(double* c, int* ic, double x, double cl, double k) {
    const int pl = DriveTables::phaseLen, fl = DriveTables::firLen;
    double* hist = c + oHist;
    const int hp = ic[0];
    hist[hp] = x;
    hist[hp + pl] = x;
    ic[0] = hp + 1 == pl ? 0 : hp + 1;
    const int base = hp + pl;
    const double inv = 1 / cl;
    double* os = c + oOs;
    for (int p = 0; p < 4; ++p) {
        double s = 0;
        if (p == 0)
            s = d.ph0Gain * hist[base - DriveTables::ph0Delay];
        else
            s = clipDot(s, d.phases.data() + p * pl, hist + base, pl);
        const double u = s * inv;
        const double u0 = c[oPrevU];
        double y;
        if (cabs(u) <= k && cabs(u0) <= k) {
            y = 0.5 * (u + u0);
            c[oPrevF] = 0.5 * u * u;
        } else {
            engaged = true;
            const double F = antideriv(u, k);
            const double du = u - u0;
            y = cabs(du) > 1e-6 ? (F - c[oPrevF]) / du : shape(0.5 * (u + u0), k);
            c[oPrevF] = F;
        }
        c[oPrevU] = u;
        const double yc = 1.25 * c[oC0] - 0.125 * (y + c[oC1]);
        c[oC1] = c[oC0];
        c[oC0] = y;
        const int op = ic[1];
        os[op] = yc * cl;
        os[op + fl] = yc * cl;
        ic[1] = op + 1 == fl ? 0 : op + 1;
    }
    const int ob = ic[1] + fl - 1;
    const int dc = DriveTables::decC;
    double out = d.decMid * os[ob - dc];
    const int lst = ob - (fl - 1);
    out = clipDotSym(out, d.decHalf.data(), os + ob, os + lst, dc);
    return out + tiny;
}

double MasterClipper::staticOut(double x, double input, double ceiling, double softness) {
    const double k = 1 - 0.6 * (softness / 100);
    const double u = std::pow(10.0, (x + input - ceiling) / 20);
    double y = u;
    if (u > k) {
        const double s = 1 - k;
        y = s > 1e-9 ? k + s * std::tanh((u - k) / s) : 1;
    }
    return ceiling + 20 * std::log10(y);
}

void MasterClipper::process(const float* inL, const float* inR, float* outL, float* outR, int n, double drive,
                            double ceiling, double softness, double bass, double bypass) {
    const double ks = kSmooth;
    const double driveT = std::pow(10.0, drive / 20);
    const double ceilT = std::pow(10.0, ceiling / 20);
    const double kneeT = 1 - 0.6 * (softness / 100);
    const double wetT = 1 - std::max(0.0, std::min(1.0, bypass));
    const double splitT = bass > 0 ? 1 : 0;
    const double fT = bass > 0 ? bass : freq;
    const double f1 = fT + (freq - fT) * kBlock;
    double* lp0 = cf;
    double* hp0 = cf + 5;
    double* lp1 = cf + 10;
    double* hp1 = cf + 15;
    double* lpc = cf + 20;
    double* hpc = cf + 25;
    butter(lp1, f1, sr, false);
    butter(hp1, f1, sr, true);
    freq = f1;
    const bool moving = cabs(f1 - fT) > 0.01 || lp0[0] != lp1[0];
    if (!moving) {
        std::copy(lp1, lp1 + 5, lpc);
        std::copy(hp1, hp1 + 5, hpc);
    }
    const int R = ring;
    double dl = driveLin, cl = ceil, kn = knee, wt = wet, spl = split;
    double mI = mIn, mO = mOut, wI = winIn, wO = winOut;
    int gN = grN;
    for (int i = 0; i < n; ++i) {
        if (moving) {
            const double a = double(i + 1) / double(n);
            for (int q = 0; q < 5; ++q) {
                lpc[q] = lp0[q] + (lp1[q] - lp0[q]) * a;
                hpc[q] = hp0[q] + (hp1[q] - hp0[q]) * a;
            }
        }
        dl = driveT + (dl - driveT) * ks;
        cl = ceilT + (cl - ceilT) * ks;
        kn = kneeT + (kn - kneeT) * ks;
        if (wt < wetT)
            wt = std::min(wetT, wt + wetStep);
        else if (wt > wetT)
            wt = std::max(wetT, wt - wetStep);
        if (spl < splitT)
            spl = std::min(splitT, spl + splitStep);
        else if (spl > splitT)
            spl = std::max(splitT, spl - splitStep);
        double inPk = 0, dryPk = 0, outPk = 0;
        for (int k = 0; k < 2; ++k) {
            double* c = st.data() + k * chSize;
            int* ic = ist.data() + k * iSize;
            const double raw = double(k == 0 ? inL[i] : inR[i]);
            const double x = raw * dl;
            const double ax = x < 0 ? -x : x;
            if (ax > inPk) inPk = ax;
            const double lo = biquad(c + oLp + 2, lpc, biquad(c + oLp, lpc, x));
            const double hi = biquad(c + oHp + 2, hpc, biquad(c + oHp, hpc, x));
            const double sp = spl;
            const double hfIn = sp * hi + (1 - sp) * x;
            const double lfIn = sp * lo;
            const double yh = hf(c, ic, hfIn, cl, kn);
            const int dp = ic[2];
            const int r = dp + 1 == R ? 0 : dp + 1;
            const double lfOut = c[oLf + r];
            const double dryOut = c[oDry + r];
            const double lin = lfOut + c[oHf + r];
            c[oLf + dp] = lfIn;
            c[oDry + dp] = raw;
            c[oHf + dp] = hfIn;
            ic[2] = r;
            const double w = lfOut + yh;
            const double y = wt * w + (1 - wt) * dryOut;
            if (k == 0)
                outL[i] = float(y);
            else
                outR[i] = float(y);
            const double al = lin < 0 ? -lin : lin;
            const double aw = w < 0 ? -w : w;
            const double ay = y < 0 ? -y : y;
            if (al > dryPk) dryPk = al;
            if (aw > outPk) outPk = aw;
            if (ay > mO) mO = ay;
        }
        if (inPk > mI) mI = inPk;
        if (dryPk > wI) wI = dryPk;
        if (outPk > wO) wO = outPk;
        ++gN;
        if (gN >= grWin) {
            winIn = wI;
            winOut = wO;
            wet = wt;
            closeWindow();
            wI = 0;
            wO = 0;
            gN = 0;
        }
    }
    driveLin = dl;
    ceil = cl;
    knee = kn;
    wet = wt;
    split = spl;
    mIn = mI;
    mOut = mO;
    winIn = wI;
    winOut = wO;
    grN = gN;
    std::copy(lp1, lp1 + 5, lp0);
    std::copy(hp1, hp1 + 5, hp0);
    meterN += n;
    if (meterN >= meterFrames) {
        frameIn = mIn;
        frameGr = frameGrAcc >= 0 ? frameGrAcc : lastGr;
        frameOut = mOut;
        ++frames;
        meterN = 0;
        mIn = 0;
        mOut = 0;
        frameGrAcc = -1;
    }
}

void MasterClipper::closeWindow() {
    double gr = 0;
    if (winIn > 1e-6 && wet > 0.5) gr = std::max(0.0, -20 * std::log10(std::max(winOut, 1e-9) / winIn));
    if (gr < 0.1 || !engaged) gr = 0;
    engaged = false;
    lastGr = gr;
    frameGrAcc = std::max(frameGrAcc, gr);
    grN = 0;
    winIn = 0;
    winOut = 0;
}

}  // namespace jambo
