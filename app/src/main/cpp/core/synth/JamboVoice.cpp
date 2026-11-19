#include "JamboVoice.h"
#include "JamboDsp.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace jambo {

using namespace dsp;
namespace { constexpr double kPi = 3.14159265358979323846; }

JamboVoice::JamboVoice(const JamboTables& tables, double sampleRate) : tables_(tables), sr_(sampleRate) {}

double JamboVoice::rand() {
    rnd_ = JamboDrift::xorshift(rnd_);
    return double(rnd_) / 4294967296.0;
}

void JamboVoice::setAnalog(int m, bool oct) {
    aOct_ = oct;
    if (m == mCur_) return;
    // SYNC включается: ведомая фаза — с фазы B (волна без разрыва).
    if (m == 1) { o3_ = a2_; sAfter_ = 0; }
    if (m == 2) pB_ = a2_;
    mPrev_ = mCur_;
    mCur_ = m;
    mMix_ = 0;
}

void JamboVoice::trigger(int e, double n, double q1, double q2, double q3, double q4, double decay,
                         double now, double tau) {
    // KICK и SUB монофоничны: новый удар (нота) в тот же слот, хвост прошлого гаснет за ~2 мс.
    dc_ = (e == J::drum && engine_ == J::drum) || (e == J::sub && engine_ == J::sub) ? last : 0;
    // SUB на звучащей ноте: фазы генераторов продолжаются.
    const bool subGoesOn = e == J::sub && engine_ == J::sub && last != 0;
    engine_ = e;
    set(n, q1, q2, q3, q4, decay);
    // Новая нота — ручки сразу на своих местах.
    s1_ = t1_; s2_ = t2_; s3_ = t3_; s4_ = t4_; sd_ = td_;
    t_ = 0;
    p1_ = 0;
    m1_ = 0;
    m2_ = 0;
    // Фаза новой ноты — как у генератора, который бежит непрерывно (frac(f·t)).
    const double f = 440 * std::pow(2.0, (note_ - 69) / 12) / sr_;
    if (!subGoesOn) {
        std::memset(hb_, 0, sizeof hb_);
        // ANALOG: DETUNE — ручка 1.3; с модуляцией расстройки нет, B на октаву ниже — вдвое медленнее.
        const double det = mCur_ == 0 ? J::analogDetune / 2 * s3_ * s3_ : 0;
        a1_ = frac(f * std::pow(2.0, det / 1200) * tau);
        a2_ = frac((aOct_ ? 0.5 : 1) * f * std::pow(2.0, -det / 1200) * tau);
        o3_ = a2_;
        pB_ = a2_;
        sAfter_ = 0;
        tri1_ = triAt(a1_);
        tri2_ = triAt(a2_);
        wCur_ = J::waveIndex(t1_);
        wPrev_ = wCur_;
    }
    wMix_ = 1;
    mPrev_ = mCur_;
    mMix_ = 1;
    bCur_ = J::waveIndex(t2_);
    bPrev_ = bCur_;
    bMix_ = 1;
    // SUB после тишины: фазы обоих осцилляторов — там, где волна проходит ноль.
    if (e == J::sub && !subGoesOn) {
        wCur_ = std::min(2, int(t1_ * 3)) + 1;
        const double p0 = wCur_ == 2 ? 0.5 : 0;
        a1_ = p0;
        a2_ = p0;
        tri1_ = 0;
        tri2_ = 0;
        wPrev_ = wCur_;
        hx_ = 0;
        hy_ = 0;
    }
    pc_ = frac(f * now);
    pm_ = frac(J::fmRatio(s1_) * f * now);
    // WTABLE: центр — так же, боковые — со случайной фазы.
    w0_ = frac(f * now);
    wl_ = rand();
    wr_ = rand();
    wl2_ = rand();
    wr2_ = rand();
    fade_ = 0;
    if (subGoesOn) {
        fade_ = J::fadeIn;
        dc_ = 0;
    }
}

void JamboVoice::render(float* out, int n, double driftRatio, WtRef table) {
    const double k = 1 - std::exp(-double(n) / (J::smooth * sr_));
    const double v1 = s1_, v2 = s2_, v3 = s3_, v4 = s4_, vd = sd_;
    s1_ = v1 + (t1_ - v1) * k;
    s2_ = v2 + (t2_ - v2) * k;
    s3From_ = v3;
    s3_ = v3 + (t3_ - v3) * k;
    s4_ = v4 + (t4_ - v4) * k;
    sd_ = vd + (td_ - vd) * k;
    const int e = engine_;
    if (e == J::drum) drum(out, n);
    else if (e == J::analog) analog(out, n, driftRatio);
    else if (e == J::fm) fm(out, n);
    else if (e == J::wtable) wtable(out, n, table);
    else if (e == J::sub) subBass(out, n);
    else std::memset(out, 0, sizeof(float) * n);
    // Начало ноты ANALOG, FM, WTABLE — плавно за fadeIn кадров (после фильтра децимации).
    int fd = fade_;
    if (fd < J::fadeIn && e != J::drum && e != J::sub) {
        for (int i = 0; i < n && fd < J::fadeIn; ++i) {
            fd += 1;
            out[i] = float(double(out[i]) * double(fd) / double(J::fadeIn));
        }
        fade_ = fd;
    }
}

// KICK: синус со свипом высоты + клик → мягкий клиппер.
void JamboVoice::drum(float* out, int n) {
    const double tauSec = 2 * kPi;
    // PITCH: 30 → 100 Гц логарифмически; нота высоту не меняет.
    const double f0 = J::drumPitchMin * std::pow(J::drumPitchMax / J::drumPitchMin, s1_);
    // Decay — время до −60 dB (постоянная = Decay / 6.9), не больше 1000 мс.
    const double decay = std::min(J::drumDecayMax, sd_);
    const double tauE = std::max(0.001, decay / 1000 / J::drumT60);
    // SWEEP: 0…+48 полутонов, время свипа = clamp(0.08 × Decay, 6, 80 мс).
    const double tauSweep = std::min(0.08, std::max(0.006, 0.08 * decay / 1000));
    const double semis = 48 * s2_;
    // DRIVE: tanh (запас 1 → 12), компенсация — линейно по куску. CLICK: уровень 0 → 1.0, длина 3 → 1 мс.
    const double dg0 = 1 + 11 * s3From_, dg1 = 1 + 11 * s3_;
    const double comp0 = J::drumSine / std::tanh(dg0 * J::drumSine), comp1 = J::drumSine / std::tanh(dg1 * J::drumSine);
    const double dgStep = (dg1 - dg0) / double(std::max(1, n)), compStep = (comp1 - comp0) / double(std::max(1, n));
    double dg = dg0, comp = comp0;
    const double click = s4_;
    const double clickLen = J::drumClick * (1.5 - s4_) * sr_;
    const double dEnv = std::exp(-1 / (tauE * sr_));
    const double dSweep = std::exp(-1 / (tauSweep * sr_));
    const double dDc = std::exp(-1 / (J::drumDeclick * sr_));
    const double sine = J::drumSine, outG = J::drumOut;
    double tt = t_;
    double env = std::exp(-tt / (tauE * sr_));
    double sw = std::exp(-tt / (tauSweep * sr_));
    double ph = p1_, d = dc_, x = 0;
    for (int i = 0; i < n; ++i) {
        const double f = f0 * std::pow(2.0, semis * sw / 12);
        ph += f / sr_;
        if (ph >= 1) ph -= 1;
        x = env * sine * std::sin(tauSec * ph);
        if (tt < clickLen) x += click * (1 - tt / clickLen);
        // Ретриг: хвост прошлого удара гаснет за ~2 мс — до клиппера.
        x += d;
        dg += dgStep;
        comp += compStep;
        out[i] = float(outG * comp * std::tanh(dg * x));
        d *= dDc;
        env *= dEnv;
        sw *= dSweep;
        tt += 1;
    }
    p1_ = ph;
    dc_ = flush(d);
    last = x;
    t_ = tt;
}

// SUB: OSC1 (нота) и OSC2 (октавой ниже, FAT), одна форма; GRIT (tanh), ×2 и полуполосный фильтр.
void JamboVoice::subBass(float* out, int n) {
    const double sr2 = sr_ * 2;
    const double* sine = tables_.sineD.data();
    const double f0 = 440 * std::pow(2.0, (note_ - 69) / 12);
    const double dt1 = std::min(0.45, f0 / sr2);
    const double dt2 = std::min(0.45, 0.5 * f0 * std::pow(2.0, J::subFat * s3_ / 1200) / sr2);
    // WAVE — ступенчатая: Triangle, Saw, Square (формы ANALOG 1–3); смена — кроссфейд за 5 мс.
    const int idx = std::min(2, int(t1_ * 3)) + 1;
    if (idx != wCur_) { wPrev_ = wCur_; wCur_ = idx; wMix_ = 0; }
    const int cur = wCur_, prev = wPrev_;
    const double gC = tables_.shapeGain[cur], gP = tables_.shapeGain[prev];
    const double xfStep = 1 / (J::analogWaveXf * sr2);
    double mix = wMix_;
    // SUB: OSC2 подмешивается к OSC1, нормировка на сумму.
    const double w1 = 1 / (1 + s2_), w2 = s2_ / (1 + s2_);
    // GRIT: tanh с усилением 0 → +24 дБ, нормировка на tanh(усиления); у нуля — прозрачно.
    const double gg = std::pow(10.0, 1.2 * s4_);
    const double wg = std::min(1.0, s4_ / 0.1);
    const double tg = std::tanh(gg);
    const double rHp = 1 - 2 * kPi * 3 / sr2;
    const double leak = 1 - 20 / sr2;
    const double dDc = std::exp(-1 / (0.002 * sr_));
    double o1 = a1_, o2 = a2_, tr1 = tri1_, tr2 = tri2_, x0 = hx_, y0 = hy_, d = dc_;
    int hp = hbPos_, fd = fade_;
    double* buf = hb_;
    const int len = J::hbLen, mid = J::hbLen / 2;
    const double* hk = tables_.hbHK;
    const double kC = tables_.hbCK;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < 2; ++j) {
            const double b1 = o1 < 0.75 ? o1 + 0.25 : o1 - 0.75, b2 = o2 < 0.75 ? o2 + 0.25 : o2 - 0.75;
            tr1 = tr1 * leak + 4 * dt1 * ((b1 < 0.5 ? 1 : -1) + blep(b1, dt1) - blep(b1 < 0.5 ? b1 + 0.5 : b1 - 0.5, dt1));
            tr2 = tr2 * leak + 4 * dt2 * ((b2 < 0.5 ? 1 : -1) + blep(b2, dt2) - blep(b2 < 0.5 ? b2 + 0.5 : b2 - 0.5, dt2));
            double x = gC * (w1 * shapeAt(sine, cur, o1, dt1, tr1) + w2 * shapeAt(sine, cur, o2, dt2, tr2));
            if (mix < 1) {
                mix = std::min(1.0, mix + xfStep);
                const double old = gP * (w1 * shapeAt(sine, prev, o1, dt1, tr1) + w2 * shapeAt(sine, prev, o2, dt2, tr2));
                x = old + (x - old) * mix;
            }
            if (wg > 0) x = (1 - wg) * x + wg * std::tanh(gg * x) / tg;
            // Постоянная составляющая — ФВЧ 3 Гц.
            const double y = x - x0 + rHp * y0;
            x0 = x;
            y0 = y;
            const double z = double(float(y));
            buf[hp] = z;
            buf[hp + len] = z;
            hp = hp + 1 == len ? 0 : hp + 1;
            o1 += dt1;
            if (o1 >= 1) o1 -= 1;
            o2 += dt2;
            if (o2 >= 1) o2 -= 1;
        }
        const double acc = half(buf + hp + mid, hk, kC);
        // Вход ноты — за fadeIn кадров; хвост прошлой ноты (d) гаснет за ~2 мс.
        double g = J::subOut;
        if (fd < J::fadeIn) {
            fd += 1;
            g *= double(fd) / double(J::fadeIn);
        }
        const double o = g * acc + d;
        out[i] = float(o);
        last = o;
        d *= dDc;
    }
    wMix_ = mix;
    a1_ = o1;
    a2_ = o2;
    tri1_ = flush(tr1);
    tri2_ = flush(tr2);
    hbPos_ = hp;
    hx_ = x0;
    hy_ = flush(y0);
    dc_ = flush(d);
    fade_ = fd;
    t_ += double(n);
}

// FM: несущая (ratio 1) и модулятор (RATIO) с обратной связью, ×2 передискретизация.
void JamboVoice::fm(float* out, int n) {
    const double* sine = tables_.sineD.data();
    const double f0 = 440 * std::pow(2.0, (note_ - 69) / 12);
    const double ratio = J::fmRatio(s1_);
    const double idxNorm = std::pow(s2_, 1.5);
    // Индекс в радианах → в долях периода.
    const double index = J::fmIndex * idxNorm / (2 * kPi);
    // FEEDBACK (1.4): 50 % — как прежде.
    const double fb = J::fmFeedback * idxNorm * s4_ * 2;
    // CONTOUR: индекс спадает с постоянной от Decay.
    const double m = s3_;
    const double k = m < 0.5 ? 2 * m : 1 + 8 * (m - 0.5);
    const double tauA = std::max(0.001, sd_ / 4000);
    const double rate = k / tauA;
    const double dt = f0 / (2 * sr_);
    double env = std::exp(-t_ / sr_ * rate);
    const double dEnv = std::exp(-rate / sr_);
    double c = pc_, md = pm_, x1 = m1_, x2 = m2_;
    int hp = hbPos_;
    double* buf = hb_;
    const int len = J::hbLen, mid = J::hbLen / 2;
    const double* hk = tables_.hbHK;
    const double kC = tables_.hbCK;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < 2; ++j) {
            const double mod = sinT(sine, md + fb * 0.5 * (x1 + x2));
            x2 = x1;
            x1 = mod;
            const double y = double(float(sinT(sine, c + index * env * mod)));
            md += ratio * dt;
            if (md >= 1) md -= floorI(md);
            c += dt;
            if (c >= 1) c -= 1;
            buf[hp] = y;
            buf[hp + len] = y;
            hp = hp + 1 == len ? 0 : hp + 1;
        }
        out[i] = float(J::fmOut * half(buf + hp + mid, hk, kC));
        env *= dEnv;
    }
    pc_ = c;
    pm_ = md;
    m1_ = x1;
    m2_ = x2;
    hbPos_ = hp;
    t_ += double(n);
}

// WTABLE: кадры таблицы по фазе и между кадрами, мипмап по октаве, WARP и UNISON.
void JamboVoice::wtable(float* out, int n, const WtRef& tb) {
    if (tb.frames <= 0 || !tb.sizes || !tb.data) {
        std::memset(out, 0, sizeof(float) * n);
        return;
    }
    const double f0 = 440 * std::pow(2.0, (note_ - 69) / 12);
    const int frames = tb.frames;
    const double fpos = s1_ * double(frames - 1);
    const int fi = std::min(frames - 2, std::max(0, int(std::floor(fpos))));
    const double ff = frames > 1 ? fpos - double(fi) : 0;
    // WARP: 0…50 % — изгиб фазы φ^(1+3w); 50…100 % — плюс синхронизация 1 → 4.
    const double w = s2_;
    const double bend = 1 + 3 * std::min(1.0, w * 2);
    const double sync = w > 0.5 ? 1 + 3 * (w - 0.5) * 2 : 1;
    // UNISON (1.3, ступенчатая): 1…5 голосов; DETUNE (1.4): 0 → 60 центов.
    const int un = 1 + std::min(4, std::max(0, int(t3_ * 5)));
    const bool even = un % 2 == 0;
    const int v0 = even ? 1 : 0;
    const double side = J::wtSide;
    const double cents = un == 1 ? 0 : J::wtDetune * s4_;
    const int sides = even ? un : un - 1;
    const double norm = J::wtOut / std::sqrt((even ? 0.0 : 1.0) + double(sides) * side * side);
    const double dt0 = f0 / sr_;
    const double dtl = dt0 * std::pow(2.0, -cents / 1200), dtr = dt0 * std::pow(2.0, cents / 1200);
    const double dtl2 = dt0 * std::pow(2.0, -cents / 2400), dtr2 = dt0 * std::pow(2.0, cents / 2400);
    // Мипмап: уровень, где гармоники (с учётом синхронизации и изгиба) не выше Найквиста.
    const double fEff = f0 * std::pow(2.0, cents / 1200) * sync * (1 + (bend - 1) * 0.5);
    const double lvf = std::ceil(std::log2(1024 * fEff / (0.5 * sr_)));
    const int lv = std::min(WtTable::levelCount - 1, std::max(0, int(std::max(-1.0, std::min(64.0, lvf)))));
    const int size = tb.sizes[lv];
    const float* data = tb.data[lv];
    const int rowA = fi * size, rowB = (frames > 1 ? fi + 1 : fi) * size;
    double c0 = w0_, cl = wl_, cr = wr_, cl2 = wl2_, cr2 = wr2_;
    const bool bent = bend != 1;
    const double sizeF = double(size);
    for (int i = 0; i < n; ++i) {
        double y = 0;
        for (int v = v0; v < v0 + un; ++v) {
            double ph = v == 0 ? c0 : (v == 1 ? cl : (v == 2 ? cr : (v == 3 ? cl2 : cr2)));
            if (sync != 1) {
                ph *= sync;
                ph -= floorI(ph);
            }
            if (bent) ph = std::pow(ph, bend);
            const double x = ph * sizeF;
            const int j = int(x);
            const double fr = x - double(j);
            const int j2 = j + 1 == size ? 0 : j + 1;
            const double a0 = data[rowA + j], a1 = data[rowA + j2];
            const double b0 = data[rowB + j], b1 = data[rowB + j2];
            const double a = a0 + (a1 - a0) * fr;
            const double b = b0 + (b1 - b0) * fr;
            y += (v == 0 ? 1 : side) * (a + (b - a) * ff);
        }
        out[i] = float(norm * y);
        c0 += dt0; if (c0 >= 1) c0 -= 1;
        cl += dtl; if (cl >= 1) cl -= 1;
        cr += dtr; if (cr >= 1) cr -= 1;
        cl2 += dtl2; if (cl2 >= 1) cl2 -= 1;
        cr2 += dtr2; if (cr2 >= 1) cr2 -= 1;
    }
    w0_ = c0; wl_ = cl; wr_ = cr; wl2_ = cl2; wr2_ = cr2;
    t_ += double(n);
}

}  // namespace jambo
