// Сатуратор мастера (порт Core/Master/Saturator.swift).
#include "Saturator.h"
#include <algorithm>
#include <cmath>

namespace jambo {

namespace {
/// F(|u|) по таблице (кубический Эрмит); за 12: tanh — линейно, EDGE — с поправкой.
inline double satTableF(const double* F, const double* D, double fmax, bool edge, double u) {
    const double a = u < 0 ? -u : u;
    if (a >= 12) {
        const double tail = edge ? (1.0 / 1728 - 1 / (a * a * a)) / 12 : 0;
        return fmax + (a - 12) - tail;
    }
    const double x = a * 32;
    const int i = int(x);
    const double tt = x - double(i), t2 = tt * tt, t3 = t2 * tt;
    const double h = 0.03125;
    return (2 * t3 - 3 * t2 + 1) * F[i] + (t3 - 2 * t2 + tt) * h * D[i] + (-2 * t3 + 3 * t2) * F[i + 1] +
           (t3 - t2) * h * D[i + 1];
}
inline double satAnti(int type, double u, const double* F, const double* D, double fmax, double lcB, double tb,
                      double td) {
    if (type == 0) return satTableF(F, D, fmax, false, u);
    if (type == 1) return (satTableF(F, D, fmax, false, u + 0.35) - lcB - u * tb) / td;
    return satTableF(F, D, fmax, true, u);
}
inline double satShapeY(int type, double u, double tb, double td) {
    if (type == 0) return std::tanh(u);
    if (type == 1) return (std::tanh(u + 0.35) - tb) / td;
    return u / std::sqrt(std::sqrt(1 + u * u * u * u));
}
inline double satDot(double acc, const double* k, const double* x, int n) {
    double s = acc;
    for (int j = 0; j < n; ++j) s += k[j] * x[-j];
    return s;
}
inline double satDotSym(double acc, const double* k, const double* a, const double* b, int n) {
    double s = acc;
    for (int j = 0; j < n; ++j) s += k[j] * (a[-j] + b[j]);
    return s;
}
}  // namespace

MasterSaturator::MasterSaturator(double sampleRate, const MasterTables& tables)
    : sr(sampleRate), t(tables), st(size_t(2 * chSize), 0.0), ist(size_t(2 * iSize), 0) {
    kSmooth = std::exp(-1 / ((smoothMs / 1000) * sampleRate));
    kBlock = std::pow(kSmooth, 128);
    fadeStep = 1 / ((typeFadeMs / 1000) * sampleRate);
    splitStep = 1 / ((smoothMs / 1000) * sampleRate);
    rampStep = 1 / ((warmRampMs / 1000) * sampleRate);
    kDc = std::exp((-2 * M_PI * dcHz) / sampleRate);
    shelf(cLo, 0, sampleRate, false);
    shelf(cHi, 0, sampleRate, true);
}

void MasterSaturator::shelf(double* o, double gDb, double sr, bool high) {
    const double A = std::pow(10.0, gDb / 40);
    const double w = (2 * M_PI * toneHz) / sr;
    const double cw = std::cos(w);
    const double al = std::sin(w) / (2 * toneQ);
    const double sa = 2 * std::sqrt(A) * al;
    const double s = high ? 1 : -1;
    const double a0 = A + 1 - s * (A - 1) * cw + sa;
    o[0] = (A * (A + 1 + s * (A - 1) * cw + sa)) / a0;
    o[1] = (-2 * s * A * (A - 1 + s * (A + 1) * cw)) / a0;
    o[2] = (A * (A + 1 + s * (A - 1) * cw - sa)) / a0;
    o[3] = (2 * s * (A - 1 - s * (A + 1) * cw)) / a0;
    o[4] = (A + 1 - s * (A - 1) * cw - sa) / a0;
}

double MasterSaturator::anti(int type, double u) const {
    const DriveTables& d = t.d;
    switch (type) {
    case 0:
        return satTableF(d.tanhF.data(), d.tanhD.data(), d.tanhMax, false, u);
    case 1:
        return (satTableF(d.tanhF.data(), d.tanhD.data(), d.tanhMax, false, u + 0.35) - d.lcB - u * d.tb) / d.td;
    default:
        return satTableF(t.edgeF.data(), t.edgeD.data(), t.edgeMax, true, u);
    }
}

void MasterSaturator::shape(double* c, int type, double k, bool add) {
    const double* u = c + oU;
    double* y = c + oY;
    double u0 = c[oPrevU];
    double F0 = c[oPrevF + type];
    const DriveTables& d = t.d;
    const bool edge = type >= 2;
    const double* tF = edge ? t.edgeF.data() : d.tanhF.data();
    const double* tD = edge ? t.edgeD.data() : d.tanhD.data();
    const double tMax = edge ? t.edgeMax : d.tanhMax;
    const double lcB = d.lcB, tb = d.tb, td = d.td;
    for (int p = 0; p < 4; ++p) {
        const double v = u[p];
        const double F = satAnti(type, v, tF, tD, tMax, lcB, tb, td);
        const double du = v - u0;
        const bool nearz = du < adaaEps && du > -adaaEps;
        const double r = nearz ? satShapeY(type, 0.5 * (v + u0), tb, td) : (F - F0) / du;
        y[p] = add ? y[p] + k * r : k * r;
        u0 = v;
        F0 = F;
    }
    c[oPrevF + type] = F0;
}

double MasterSaturator::wet(double* c, int* ic, double x, int ta, int tb, double wa, double wb, double ca, double cb) {
    const DriveTables& d = t.d;
    const int pl = DriveTables::phaseLen;
    double* hist = c + oHist;
    const int hp = ic[0];
    hist[hp] = x;
    hist[hp + pl] = x;
    ic[0] = hp + 1 == pl ? 0 : hp + 1;
    const int base = hp + pl;
    double* u = c + oU;
    u[0] = d.ph0Gain * hist[base - DriveTables::ph0Delay];
    for (int p = 1; p < 4; ++p) u[p] = satDot(0, d.phases.data() + p * pl, hist + base, pl);
    if (wa > 0) {
        shape(c, ta, wa / ca, false);
        shape(c, tb, wb / cb, true);
    } else {
        shape(c, tb, 1 / cb, false);
    }
    c[oPrevU] = u[3];
    const double* y = c + oY;
    double* cbuf = c + oCorr;
    double* os = c + oOs;
    const int cl = corrLen, fl = DriveTables::firLen;
    const double k0 = -1.0 / 128, k1 = 9.0 / 128, k2 = -37.0 / 128, k3 = 93.0 / 128;
    int cp = ic[1];
    int op = ic[2];
    for (int p = 0; p < 4; ++p) {
        cbuf[cp] = y[p];
        cbuf[cp + cl] = y[p];
        const int e = cp + cl;
        cp = cp + 1 == cl ? 0 : cp + 1;
        const double yc = k0 * (cbuf[e] + cbuf[e - 7]) + k1 * (cbuf[e - 1] + cbuf[e - 6]) +
                          k2 * (cbuf[e - 2] + cbuf[e - 5]) + k3 * (cbuf[e - 3] + cbuf[e - 4]);
        os[op] = yc;
        os[op + fl] = yc;
        op = op + 1 == fl ? 0 : op + 1;
    }
    ic[1] = cp;
    ic[2] = op;
    const int ob = op + fl - 1;
    const int dc = DriveTables::decC;
    double out = d.decMid * os[ob - dc];
    const int lst = ob - (fl - 1);
    out = satDotSym(out, d.decHalf.data(), os + ob, os + lst, dc);
    return out + tiny;
}

void MasterSaturator::process(const float* inL, const float* inR, float* outL, float* outR, int n, double dIn,
                              double tIn, double toIn, double mIn, double fIn) {
    const double ks = kSmooth;
    const double driveT = std::max(0.0, std::min(MasterTables::maxDriveDb, dIn));
    const double mixT = std::max(0.0, std::min(1.0, mIn / 100));
    const int type = int(std::max(0.0, std::min(2.0, std::floor(tIn + 0.5))));
    if (type != cur) {
        prev = fade >= 0.5 ? cur : prev;
        cur = type;
        fade = 0;
        for (int k = 0; k < 2; ++k) {
            double* c = st.data() + k * chSize;
            c[oPrevF + type] = anti(type, c[oPrevU]);
        }
    }
    const double toneT = (std::max(-100.0, std::min(100.0, toIn)) / 100) * toneMaxDb;
    if (toneDb != toneT) {
        toneDb = std::abs(toneT - toneDb) < 1e-4 ? toneT : toneT + (toneDb - toneT) * kBlock;
        shelf(cLo, -toneDb, sr, false);
        shelf(cHi, toneDb, sr, true);
    }
    const double fT = std::max(focusOff, std::min(2000.0, fIn));
    const double splitT = fT > focusOff + 1e-3 ? 1 : 0;
    if (splitT > 0) focus = std::exp(std::log(fT) + (std::log(focus) - std::log(fT)) * kBlock);
    if (std::abs(focus - fT) < 1e-3) focus = fT;
    const double kLp0 = kLp;
    kLp = 1 - std::exp((-2 * M_PI * focus) / sr);
    const double kLpStep = (kLp - kLp0) / double(n);
    const int D = dryDelay;
    if ((driveT == 0 && driveDb < 1e-4) || (mixT == 0 && mix < 1e-5)) {
        processIdle(inL, inR, outL, outR, n, driveT, mixT, splitT, kLp0, kLpStep);
        return;
    }
    if (idle) {
        idle = false;
        for (int k = 0; k < 2; ++k) {
            double* c = st.data() + k * chSize;
            std::fill(c + oCorr, c + oCorr + 2 * corrLen + 2 * DriveTables::firLen, 0.0);
            std::fill(c + oPrevF, c + oPrevF + 3, 0.0);
            c[oPrevU] = 0;
            c[oDcX] = 0;
            c[oDcY] = 0;
        }
        warmN = warmSamples;
        ramp = 0;
    }
    const double* comp = t.comp.data();
    const int cn = MasterTables::compN + 2;
    double dBv = driveDb, mixv = mix, spl = split, fd = fade;
    int warm = warmN;
    double rmp = ramp;
    const int ta = prev, tb = cur;
    double lastDb = -1, g = 1, tt = 0, ca = 1, cb = 1;
    for (int i = 0; i < n; ++i) {
        dBv = driveT + (dBv - driveT) * ks;
        mixv = mixT + (mixv - mixT) * ks;
        if (spl < splitT)
            spl = std::min(splitT, spl + splitStep);
        else if (spl > splitT)
            spl = std::max(splitT, spl - splitStep);
        if (fd < 1) fd = std::min(1.0, fd + fadeStep);
        const double dB = dBv;
        if (dB != lastDb) {
            lastDb = dB;
            g = std::exp(dB * 0.11512925464970229);
            tt = std::min(1.0, dB / fadeInDb);
            const double ci = dB / MasterTables::compStepDb;
            const int i0 = int(ci);
            const double fr = ci - double(i0);
            ca = comp[ta * cn + i0] + (comp[ta * cn + i0 + 1] - comp[ta * cn + i0]) * fr;
            cb = comp[tb * cn + i0] + (comp[tb * cn + i0 + 1] - comp[tb * cn + i0]) * fr;
        }
        if (warm > 0)
            --warm;
        else if (rmp < 1)
            rmp = std::min(1.0, rmp + rampStep);
        const double w = mixv * tt * tt * (3 - 2 * tt) * rmp;
        const double wb = fd;
        const double wa = ta == tb ? 0 : 1 - wb;
        const double tube = (tb == 1 ? wb : 0) + (ta == 1 ? wa : 0);
        const double sp = spl;
        const double kl = kLp0 + kLpStep * double(i + 1);
        for (int k = 0; k < 2; ++k) {
            double* c = st.data() + k * chSize;
            int* ic = ist.data() + k * iSize;
            const double x = double(k == 0 ? inL[i] : inR[i]);
            const int dp = ic[3];
            const double dry = c[oDry + dp];
            c[oDry + dp] = x;
            c[oLp] += kl * (x - c[oLp]);
            const double lpOut = c[oLpD + dp];
            const double lpS = sp * c[oLp];
            c[oLpD + dp] = lpS;
            ic[3] = dp + 1 == D ? 0 : dp + 1;
            double y = wet(c, ic, (x - lpS) * g, ta, tb, wa, wb, ca, cb);
            const double hpY = kDc * (c[oDcY] + y - c[oDcX]) + tiny;
            c[oDcX] = y;
            c[oDcY] = hpY;
            y += tube * (hpY - y);
            double* lo = c + oLo;
            double* hi = c + oHi;
            const double y1 = cLo[0] * y + lo[0];
            lo[0] = cLo[1] * y - cLo[3] * y1 + lo[1];
            lo[1] = cLo[2] * y - cLo[4] * y1;
            const double y2 = cHi[0] * y1 + hi[0];
            hi[0] = cHi[1] * y1 - cHi[3] * y2 + hi[1];
            hi[1] = cHi[2] * y1 - cHi[4] * y2;
            const double yo = dry + w * (y2 + lpOut - dry);
            if (k == 0)
                outL[i] = float(yo);
            else
                outR[i] = float(yo);
        }
    }
    driveDb = dBv;
    mix = mixv;
    split = spl;
    fade = fd;
    warmN = warm;
    ramp = rmp;
}

void MasterSaturator::processIdle(const float* inL, const float* inR, float* outL, float* outR, int n, double driveT,
                                  double mixT, double splitT, double kLp0, double kLpStep) {
    split = splitT;
    driveDb = driveT;
    mix = mixT;
    fade = 1;
    idle = true;
    const double g = std::exp(driveT * 0.11512925464970229);
    const int D = dryDelay, pl = DriveTables::phaseLen;
    const double sp = split;
    for (int k = 0; k < 2; ++k) {
        double* c = st.data() + k * chSize;
        int* ic = ist.data() + k * iSize;
        const float* src = k == 0 ? inL : inR;
        float* o = k == 0 ? outL : outR;
        for (int i = 0; i < n; ++i) {
            const double x = double(src[i]);
            const int hp = ic[0];
            c[oHist + hp] = x * g;
            c[oHist + hp + pl] = x * g;
            ic[0] = hp + 1 == pl ? 0 : hp + 1;
            const int dp = ic[3];
            o[i] = float(c[oDry + dp]);
            c[oDry + dp] = x;
            c[oLp] += (kLp0 + kLpStep * double(i + 1)) * (x - c[oLp]);
            c[oLpD + dp] = sp * c[oLp];
            ic[3] = dp + 1 == D ? 0 : dp + 1;
        }
    }
}

}  // namespace jambo
