// ANALOG (порт 1:1 Core/Jambo/JamboAnalog.swift): A и B, модуляция B осциллятором A — SYNC, FM, RING, AM;
// AMOUNT — ручка 1.3, MIX — A ↔ B. Смена режима — кроссфейд за 5 мс. ×2 передискретизация и полуполосный фильтр.
#include "JamboVoice.h"
#include "JamboDsp.h"
#include <cmath>
#include <algorithm>

namespace jambo {

using namespace dsp;

namespace {
constexpr double kPi = 3.14159265358979323846;

/// Отсчёт формы k в редких местах: how 0 — свободный осциллятор (треугольник — интеграл tri), 1 — без сглаживания,
/// 2 — PolyBLEP по шагу dt (треугольник — по фазе).
double wave(const double* sine, int k, double a, double dt, double tri, int how) {
    if (k == 0) return sinT(sine, a);
    if (k == 1) return how == 0 ? tri : triAt(a);
    if (how == 1) return k == 2 ? 1 - 2 * a : (a < 0.5 ? 1 : -1);
    if (k == 2) return 1 - 2 * a + blep(a, dt);
    return (a < 0.5 ? 1 : -1) + blep(a, dt) - blep(a < 0.5 ? a + 0.5 : a - 0.5, dt);
}
}  // namespace

void JamboVoice::analog(float* out, int n, double ratio) {
    const double sr2 = sr_ * 2;
    const double* sine = tables_.sineD.data();
    const double f0 = 440 * std::pow(2.0, (note_ - 69) / 12) * ratio;
    const double fB = (aOct_ ? 0.5 : 1) * f0;
    const double amt = std::min(1.0, std::max(0.0, s3_));
    // DETUNE (1.3, без модуляции): A вверх, B вниз, в сумме 0…60 центов по кривой x².
    const double det = mCur_ == 0 ? J::analogDetune / 2 * s3_ * s3_ : 0;
    const double dt1 = std::min(0.45, f0 * std::pow(2.0, det / 1200) / sr2);
    const double dt2 = std::min(0.45, fB * std::pow(2.0, -det / 1200) / sr2);
    // SYNC: B выше A на 0…24 полутона, FM: индекс по AMOUNT².
    const double dt3 = std::min(0.45, fB * std::pow(2.0, amt * 2) / sr2);
    const double fmIdx = J::analogFmIndex * amt * amt;
    const int ia = J::waveIndex(t1_);
    if (ia != wCur_) { wPrev_ = wCur_; wCur_ = ia; wMix_ = 0; }
    const int ib = J::waveIndex(t2_);
    if (ib != bCur_) { bPrev_ = bCur_; bCur_ = ib; bMix_ = 0; }
    const int ca = wCur_, pa = wPrev_, cb = bCur_, pb = bPrev_;
    const double gA = tables_.shapeGain[ca], gAp = tables_.shapeGain[pa];
    const double gB = tables_.shapeGain[cb], gBp = tables_.shapeGain[pb];
    const double xfStep = 1 / (J::analogWaveXf * sr2);
    double mixA = wMix_, mixB = bMix_;
    const double ang = s4_ * kPi / 2;
    const double ka = std::cos(ang), kb = std::sin(ang);
    const double wa = J::analogOut * ka / (ka + kb), wb = J::analogOut * kb / (ka + kb);
    const int cm = mCur_, pm = mPrev_;
    double mx = mMix_;
    const bool needS = cm == 1 || (mx < 1 && pm == 1);
    const bool needF = cm == 2 || (mx < 1 && pm == 2);
    const double leak = 1 - 20 / sr2;
    double o1 = a1_, o2 = a2_, tr1 = tri1_, tr2 = tri2_, s = o3_, pf = pB_, after = sAfter_;
    int hp = hbPos_;
    double* buf = hb_;
    const int len = J::hbLen, mid = J::hbLen / 2;
    const double* hk = tables_.hbHK;
    const double kC = tables_.hbCK;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < 2; ++j) {
            const double b1 = o1 < 0.75 ? o1 + 0.25 : o1 - 0.75, b2 = o2 < 0.75 ? o2 + 0.25 : o2 - 0.75;
            tr1 = tr1 * leak + 4 * dt1 * ((b1 < 0.5 ? 1 : -1) + blep(b1, dt1) - blep(b1 < 0.5 ? b1 + 0.5 : b1 - 0.5, dt1));
            tr2 = tr2 * leak + 4 * dt2 * ((b2 < 0.5 ? 1 : -1) + blep(b2, dt2) - blep(b2 < 0.5 ? b2 + 0.5 : b2 - 0.5, dt2));
            // Смена формы — кроссфейд 5 мс.
            double xa = gA * shapeAt(sine, ca, o1, dt1, tr1);
            if (mixA < 1) {
                mixA = std::min(1.0, mixA + xfStep);
                const double old = gAp * wave(sine, pa, o1, dt1, tr1, 0);
                xa = old + (xa - old) * mixA;
            }
            double xb = gB * shapeAt(sine, cb, o2, dt2, tr2);
            if (mixB < 1) {
                mixB = std::min(1.0, mixB + xfStep);
                const double old = gBp * wave(sine, pb, o2, dt2, tr2, 0);
                xb = old + (xb - old) * mixB;
            }
            double vS = 0, vF = 0;
            // SYNC: ведомый B — форма без сглаживания; все его скачки — поправкой PolyBLEP ниже.
            const double kS = mixB;
            if (needS) {
                vS = gB * wave(sine, cb, s, dt3, 0, 1);
                if (kS < 1) {
                    const double old = gBp * wave(sine, pb, s, dt3, 0, 1);
                    vS = old + (vS - old) * kS;
                }
            }
            if (needF) {
                double p = o2 + fmIdx * xa;
                p -= floorI(p);
                double d = p - pf;
                if (d > 0.5) d -= 1; else if (d < -0.5) d += 1;
                const double dte = std::min(0.45, std::max(1e-5, std::fabs(d)));
                vF = gB * wave(sine, cb, p, dte, 0, 2);
                if (kS < 1) {
                    const double old = gBp * wave(sine, pb, p, dte, 0, 2);
                    vF = old + (vF - old) * kS;
                }
                pf = p;
            } else {
                pf = o2;
            }
            const double vR = xb + (xa * xb - xb) * amt;
            // AM: однополярная — B · (1 − AMOUNT·(1 − A)/2).
            const double vA = xb + xb * amt * (xa - 1) * 0.5;
            double bOut = cm == 0 ? xb : (cm == 1 ? vS : (cm == 2 ? vF : (cm == 3 ? vR : vA)));
            double wS = cm == 1 ? 1 : 0;
            if (mx < 1) {
                mx = std::min(1.0, mx + xfStep);
                const double old = pm == 0 ? xb : (pm == 1 ? vS : (pm == 2 ? vF : (pm == 3 ? vR : vA)));
                bOut = old + (bOut - old) * mx;
                wS = (cm == 1 ? mx : 0) + (pm == 1 ? 1 - mx : 0);
            }
            double y = wa * xa + wb * bOut;
            if (after != 0) { y += after; after = 0; }
            o1 += dt1;
            const bool wrap = o1 >= 1;
            if (wrap) o1 -= 1;
            o2 += dt2;
            if (o2 >= 1) o2 -= 1;
            if (needS) {
                // События до следующего отсчёта (τ — доля отсчёта): свои скачки ведомого до сброса и сам сброс.
                // Скачок h в τ: этому отсчёту + h/2·(1 − τ)², следующему − h/2·τ².
                const double tr = wrap ? 1 - o1 / dt1 : 2;
                const double g = wS * wb;
                double corr = 0, nextC = 0;
                if (cb >= 2 || (kS < 1 && pb >= 2)) {
                    const double jc0 = cb >= 2 ? 2 * gB : 0, jp0 = pb >= 2 ? 2 * gBp : 0;
                    const double jc5 = cb == 3 ? -2 * gB : 0, jp5 = pb == 3 ? -2 * gBp : 0;
                    const double j0 = (jp0 + (jc0 - jp0) * kS) * g, j5 = (jp5 + (jc5 - jp5) * kS) * g;
                    const double tw = (1 - s) / dt3;
                    if (tw <= 1 && tw < tr) {
                        corr += j0 / 2 * (1 - tw) * (1 - tw);
                        nextC -= j0 / 2 * tw * tw;
                    }
                    if (s < 0.5) {
                        const double th = (0.5 - s) / dt3;
                        if (th <= 1 && th < tr) {
                            corr += j5 / 2 * (1 - th) * (1 - th);
                            nextC -= j5 / 2 * th * th;
                        }
                    }
                }
                double sn = s + dt3;
                if (sn >= 1) sn -= 1;
                if (wrap) {
                    // Сброс в τ = tr: фаза ведомого в этот миг → 0, затем ещё (1 − tr)·dt3.
                    double at = s + dt3 * tr;
                    if (at >= 1) at -= 1;
                    double oldV = gB * wave(sine, cb, at, 0, 0, 1);
                    double newV = gB * wave(sine, cb, 0, 0, 0, 1);
                    if (kS < 1) {
                        const double po = gBp * wave(sine, pb, at, 0, 0, 1);
                        const double pn = gBp * wave(sine, pb, 0, 0, 0, 1);
                        oldV = po + (oldV - po) * kS;
                        newV = pn + (newV - pn) * kS;
                    }
                    const double h = (newV - oldV) * g;
                    corr += h / 2 * (1 - tr) * (1 - tr);
                    nextC -= h / 2 * tr * tr;
                    sn = (1 - tr) * dt3;
                }
                y += corr;
                after = nextC;
                s = sn;
            } else {
                s = o2;
            }
            const double yf = double(float(y));
            buf[hp] = yf;
            buf[hp + len] = yf;
            hp = hp + 1 == len ? 0 : hp + 1;
        }
        out[i] = float(half(buf + hp + mid, hk, kC));
    }
    wMix_ = mixA;
    bMix_ = mixB;
    mMix_ = mx;
    if (mx >= 1) mPrev_ = cm;
    a1_ = o1;
    a2_ = o2;
    o3_ = s;
    pB_ = pf;
    sAfter_ = after;
    tri1_ = flush(tr1);
    tri2_ = flush(tr2);
    hbPos_ = hp;
    t_ += double(n);
}

}  // namespace jambo
