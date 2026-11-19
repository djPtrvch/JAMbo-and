// Лимитер мастера (порт Core/Master/Limiter.swift).
#include "Limiter.h"
#include <algorithm>
#include <cmath>

namespace jambo {

LimiterGain::LimiterGain(int maxL_, int L_)
    : maxL(maxL_), tHist(size_t(maxL_), 1.0), eHist(size_t(maxL_), 1.0), qIdx(size_t(maxL_ + 1), 0),
      qVal(size_t(maxL_ + 1), 0.0), L(L_), sum(double(L_)) {}

void LimiterGain::setWindow(int L_) {
    L = L_;
    const int M = maxL;
    qHead = 0;
    qLen = 0;
    sum = 0;
    for (int k = L + hold - 1; k >= 0; --k) {
        const int pos = (hpos - 1 - k + 2 * M) % M;
        (void)pushMin(n - 1 - k, tHist[size_t(pos)]);
        if (k < L) sum += eHist[size_t(pos)];
    }
    sumAge = 0;
}

double LimiterGain::pushMin(int idx, double v) {
    const int cap = maxL + 1;
    int head = qHead;
    int len = qLen;
    while (len > 0) {
        const int tail = (head + len - 1) % cap;
        if (qVal[size_t(tail)] >= v)
            --len;
        else
            break;
    }
    const int at = (head + len) % cap;
    qIdx[size_t(at)] = idx;
    qVal[size_t(at)] = v;
    ++len;
    const int oldest = idx - L - hold;
    while (qIdx[size_t(head)] <= oldest) {
        head = (head + 1) % cap;
        --len;
    }
    qHead = head;
    qLen = len;
    return qVal[size_t(head)];
}

double LimiterGain::step(double pk, double ceil, bool autoRel, double kRel, double kFast, double kSlow) {
    const int M = maxL;
    const double t = pk > ceil ? ceil / pk : 1;
    const int hp = hpos;
    tHist[size_t(hp)] = t;
    const double h = pushMin(n, t);
    double e;
    if (autoRel) {
        envF = h < envF ? h : h + (envF - h) * kFast;
        envS = h + (envS - h) * kSlow;
        e = std::min(envF, envS);
    } else {
        envF = h < envF ? h : h + (envF - h) * kRel;
        envS = envF;
        e = envF;
    }
    const double old = eHist[size_t((hp - L + 2 * M) % M)];
    eHist[size_t(hp)] = e;
    sum += e - old;
    ++sumAge;
    if (sumAge >= 4096) {
        double s = 0;
        for (int k = 0; k < L; ++k) s += eHist[size_t((hp - k + 2 * M) % M)];
        sum = s;
        sumAge = 0;
    }
    hpos = (hp + 1) % M;
    ++n;
    return std::min(1.0, sum / double(L));
}

int MasterLimiter::look(double ms, double sr, int maxL) {
    const int v = int(std::floor((ms / 1000) * sr + 0.5));
    return std::max(1, std::min(maxL - LimiterGain::hold - 1, v));
}

static int maxLOf(double sr) { return int(std::ceil((MasterLimiter::maxLookaheadMs / 1000) * sr)) + LimiterGain::hold + 2; }

MasterLimiter::MasterLimiter(double sampleRate, const MasterTables& tables)
    : sr(sampleRate), phases(tables.limPhases.data()), maxL(maxLOf(sampleRate)),
      size(maxLOf(sampleRate) + MasterTables::limFirDelay + 8), L(look(3, sampleRate, maxLOf(sampleRate))),
      gc(maxLOf(sampleRate), look(3, sampleRate, maxLOf(sampleRate))) {
    kSmooth = std::exp(-1 / ((smoothMs / 1000) * sampleRate));
    wetStep = 1 / ((bypassMs / 1000) * sampleRate);
    xfStep = 1 / ((xfadeMs / 1000) * sampleRate);
    kFast = std::exp(-1 / ((autoFastMs / 1000) * sampleRate));
    kSlow = std::exp(-1 / ((autoSlowMs / 1000) * sampleRate));
    kRel = std::exp(-1 / ((150.0 / 1000) * sampleRate));
    ceil = std::pow(10.0, -1.0 / 20);
    dl.assign(size_t(size), 0.f);
    dr.assign(size_t(size), 0.f);
    xl.assign(size_t(size), 0.f);
    xr.assign(size_t(size), 0.f);
    hIn.assign(size_t(4 * taps), 0.0);
    hOut.assign(size_t(4 * taps), 0.0);
}

double MasterLimiter::truePeak(double* h, int& pos, double l, double r) {
    const int T = taps;
    const int p = pos;
    const double fl = double(float(l)), fr = double(float(r));
    h[p] = fl;
    h[p + T] = fl;
    h[2 * T + p] = fr;
    h[2 * T + p + T] = fr;
    pos = p + 1 == T ? 0 : p + 1;
    const double* hl = h + p + T;
    const double* hr = h + 2 * T + p + T;
    double m = 0;
    for (int q = 0; q < 4; ++q) {
        const double* kp = phases + q * T;
        double sl = 0, s2 = 0;
        for (int j = 0; j < T; ++j) {
            sl += kp[j] * hl[-j];
            s2 += kp[j] * hr[-j];
        }
        const double a = std::max(std::abs(sl), std::abs(s2));
        if (a > m) m = a;
    }
    return m;
}

void MasterLimiter::process(const float* inL, const float* inR, float* outL, float* outR, int n, double gain,
                            double ceiling, double lookahead, double rel, bool autoRel, double bypass, double guardDb) {
    if (rel != relSig) {
        relSig = rel;
        kRel = rel <= 0 ? 0 : std::exp(-1 / ((rel / 1000) * sr));
    }
    const double ks = kSmooth;
    const double gainT = guardCut > 0 ? std::pow(10.0, (gain - guardCut) / 20) : std::pow(10.0, gain / 20);
    const double ceilT = std::pow(10.0, ceiling / 20);
    const double wetT = 1 - std::max(0.0, std::min(1.0, bypass));
    const int S = size;
    const int Lt = look(lookahead, sr, maxL);
    if (Lt != L && xf >= 1) {
        oldDelay = delayFor(L);
        xf = 0;
        L = Lt;
        gc.setWindow(Lt);
    }
    double gLin = gainLin, cl = ceil, wtv = wet, pkP = pkPrev, mI = mIn, mG = mGr, mO = mOut, xfv = xf;
    int wpos = w;
    double bG = 1;
    const int delay = delayFor(L), od = oldDelay;
    for (int i = 0; i < n; ++i) {
        gLin = gainT + (gLin - gainT) * ks;
        cl = ceilT + (cl - ceilT) * ks;
        if (wtv < wetT)
            wtv = std::min(wetT, wtv + wetStep);
        else if (wtv > wetT)
            wtv = std::max(wetT, wtv - wetStep);
        const double rawL = double(inL[i]), rawR = double(inR[i]);
        const double gl = rawL * gLin;
        const double gr = rawR * gLin;
        const int wi = wpos;
        dl[size_t(wi)] = float(gl);
        dr[size_t(wi)] = float(gr);
        xl[size_t(wi)] = float(rawL);
        xr[size_t(wi)] = float(rawR);
        wpos = (wi + 1) % S;
        const double pk0 = truePeak(hIn.data(), posIn, gl, gr);
        const double pk = std::max(pk0, pkP);
        pkP = pk0;
        if (pk0 > mI) mI = pk0;
        const double c = cl;
        const double g = gc.step(pk, c, autoRel, kRel, kFast, kSlow);
        if (g < mG) mG = g;
        if (g < bG) bG = g;
        const size_t r = size_t((wi - delay + S) % S);
        double yl = double(dl[r]), yr = double(dr[r]);
        double bl = double(xl[r]), br = double(xr[r]);
        if (xfv < 1) {
            const size_t ro = size_t((wi - od + S) % S);
            const double a = xfv;
            yl = yl * a + double(dl[ro]) * (1 - a);
            yr = yr * a + double(dr[ro]) * (1 - a);
            bl = bl * a + double(xl[ro]) * (1 - a);
            br = br * a + double(xr[ro]) * (1 - a);
            xfv = std::min(1.0, xfv + xfStep);
        }
        yl *= g;
        yr *= g;
        if (yl > c)
            yl = c;
        else if (yl < -c)
            yl = -c;
        if (yr > c)
            yr = c;
        else if (yr < -c)
            yr = -c;
        const double wt = wtv;
        const double ol = yl * wt + bl * (1 - wt);
        const double orr = yr * wt + br * (1 - wt);
        const double po = truePeak(hOut.data(), posOut, ol, orr);
        if (po > mO) mO = po;
        outL[i] = float(ol);
        outR[i] = float(orr);
    }
    gainLin = gLin;
    guardStep(guardDb, wtv > 0.5 ? -20 * std::log10(std::max(bG, 1e-6)) : 0, double(n) / sr);
    ceil = cl;
    wet = wtv;
    w = wpos;
    pkPrev = pkP;
    mIn = mI;
    mGr = mG;
    mOut = mO;
    xf = xfv;
    meterN += n;
    if (meterN >= meterFrames) {
        frameIn = mIn;
        frameGr = wet > 0.5 ? -20 * std::log10(std::max(mGr, 1e-6)) : 0;
        frameOut = mOut;
        ++frames;
        meterN = 0;
        mIn = 0;
        mGr = 1;
        mOut = 0;
    }
}

void MasterLimiter::guardStep(double limit, double gr, double dt) {
    guardAvg += (gr - guardAvg) * (1 - std::exp(-dt / guardAvgS));
    if (limit <= 0) {
        guardCut = std::max(0.0, guardCut - guardOff * dt);
        guardHold = 0;
        return;
    }
    if (guardAvg > limit) {
        guardCut = std::min(guardMax, guardCut + guardDown * dt);
        guardHold = guardHoldS;
    } else if (guardHold > 0) {
        guardHold -= dt;
    } else if (guardAvg < limit - 1) {
        guardCut = std::max(0.0, guardCut - guardUp * dt);
    }
}

}  // namespace jambo
