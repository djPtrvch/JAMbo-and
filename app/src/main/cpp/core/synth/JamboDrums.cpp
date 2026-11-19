#include <cstdint>
#include "JamboDrums.h"
#include "JamboDsp.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace jambo {

using namespace dsp;

namespace {
constexpr double kPi = 3.14159265358979323846;

inline int64_t xr(int64_t r0) {
    int64_t r = r0;
    r ^= (r << 13) & 0xFFFFFFFFLL;
    r ^= r >> 17;
    r ^= (r << 5) & 0xFFFFFFFFLL;
    return r;
}
/// Белый шум −1…1 (xorshift32).
inline double noiseOf(int64_t& r) {
    r = xr(r);
    return double(r) / 2147483648.0 - 1;
}
/// Выход: стык ретрига (+ хвост прошлого удара) → tanh.
inline double outTanh(float* out, int i, double x, double& d, double dDc) {
    const double y = std::tanh(x + d);
    out[i] = float(y);
    d *= dDc;
    return y;
}
/// Коэффициенты TPT SVF: a1, a2, a3 и k = 1/Q.
struct Svf {
    double a1 = 0, a2 = 0, a3 = 0, k = 1;
    Svf(double hz, double q, double sr) {
        const double f = std::min(hz, sr * 0.45);
        const double g = std::tan(kPi * f / sr);
        k = 1 / q;
        a1 = 1 / (1 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};
}  // namespace

JamboDrumVoice::JamboDrumVoice(const JamboTables& tables, double sampleRate) : tables_(tables), sr_(sampleRate) {}

void JamboDrumVoice::trigger(int e, double q1, double q2, double q3, double q4, double decay) {
    dc_ = last;
    engine_ = e;
    set(q1, q2, q3, q4, decay);
    s1_ = t1_; s2_ = t2_; s3_ = t3_; s4_ = t4_; sd_ = td_;
    t_ = 0;
    std::memset(ph_, 0, sizeof ph_);
    std::memset(ic_, 0, sizeof ic_);
    std::memset(hb_, 0, sizeof hb_);
    hbPos_ = 0;
    lp_ = 0;
    if (e == J::hat) {
        // Фазы квадратов — разные (как у свободно бегущих генераторов 808).
        for (int k = 0; k < 6; ++k) {
            rnd_ = xr(rnd_);
            ph_[k] = double(rnd_) / 4294967296.0;
        }
    }
    if (e == J::clap) planBursts();
    envC_ = 0;
    envT_ = 0;
}

void JamboDrumVoice::planBursts() {
    bursts_ = std::min(6, std::max(1, int(t2_ * 6) + 1));
    const double gap = (J::clapGapMin + (J::clapGapMax - J::clapGapMin) * std::pow(s3_, 1.2)) / 1000 * sr_;
    const double jitter = s3_ > 0.6 ? 0.4 * (s3_ - 0.6) / 0.4 : 0;
    double at = 0;
    for (int k = 0; k < bursts_; ++k) {
        burstAt_[k] = at;
        const double r = noiseOf(rnd_);
        at += gap * (1 + jitter * r);
    }
}

void JamboDrumVoice::render(float* out, int n) {
    const double k = 1 - std::exp(-double(n) / (J::smooth * sr_));
    s1_ += (t1_ - s1_) * k;
    s2_ += (t2_ - s2_) * k;
    s3_ += (t3_ - s3_) * k;
    s4_ += (t4_ - s4_) * k;
    sd_ += (td_ - sd_) * k;
    if (engine_ == J::snare) snare(out, n);
    else if (engine_ == J::hat) hat(out, n);
    else if (engine_ == J::clap) clap(out, n);
    else perc(out, n);
}

// SNARE: два синуса корпуса (f, 1.8·f) с падением высоты + шум через полосовой.
void JamboDrumVoice::snare(float* out, int n) {
    const double* sine = tables_.sineD.data();
    const double f = 120 * std::pow(400.0 / 120, s1_);
    const double dec = std::min(800.0, std::max(30.0, sd_)) / 1000;
    const double tauN = dec / 6.9, tauB = tauN * 0.6;
    const double semis = 12 * s4_;
    const double tauS = (0.010 + 0.020 * s4_) / 3;
    const double ang = s2_ * kPi / 2;
    const double gB = std::cos(ang), gN = std::sin(ang) * 2.2;
    const Svf fl(800 * std::pow(10.0, s3_), 0.8, sr_);
    const double click = 0.6 * s4_, clickLen = 0.001 * sr_;
    const double dEnvN = std::exp(-1 / (tauN * sr_)), dEnvB = std::exp(-1 / (tauB * sr_)), dSw = std::exp(-1 / (tauS * sr_));
    const double dDc = std::exp(-1 / (0.002 * sr_));
    double tt = t_;
    double envN = std::exp(-tt / (tauN * sr_)), envB = std::exp(-tt / (tauB * sr_)), sw = std::exp(-tt / (tauS * sr_));
    double p0 = ph_[0], p1 = ph_[1], c1 = ic_[0], c2 = ic_[1], d = dc_;
    const double ln2 = 0.057762265;
    int64_t rn = rnd_;
    double ly = last;
    for (int i = 0; i < n; ++i) {
        const double fb = f * std::exp(ln2 * semis * sw);
        p0 += fb / sr_;
        if (p0 >= 1) p0 -= 1;
        p1 += 1.8 * fb / sr_;
        if (p1 >= 1) p1 -= 1;
        double body = envB * (sinT(sine, p0) + 0.5 * sinT(sine, p1)) / 1.5;
        if (tt < clickLen) body += click * (1 - tt / clickLen);
        // Шум: полосовой (TPT SVF).
        const double x = noiseOf(rn);
        const double v3 = x - c2;
        const double v1 = fl.a1 * c1 + fl.a2 * v3;
        const double v2 = c2 + fl.a2 * c1 + fl.a3 * v3;
        c1 = 2 * v1 - c1;
        c2 = 2 * v2 - c2;
        ly = outTanh(out, i, gB * body + gN * envN * v1, d, dDc);
        envN *= dEnvN;
        envB *= dEnvB;
        sw *= dSw;
        tt += 1;
    }
    ph_[0] = p0; ph_[1] = p1;
    ic_[0] = flush(c1); ic_[1] = flush(c2);
    rnd_ = rn;
    last = ly;
    dc_ = flush(d);
    t_ = tt;
}

// HAT (808): шесть квадратов (×2, PolyBLEP) или шум → два полосовых → ФВЧ.
void JamboDrumVoice::hat(float* out, int n) {
    const double sr2 = sr_ * 2;
    const double tune = std::pow(2.0, 2 * s1_ - 1);
    const double spread = 2 * s2_;
    const double tone = std::pow(2.0, 2 * s3_ - 1);
    const double ang = s4_ * kPi / 2;
    const double gM = std::cos(ang), gN = std::sin(ang) * 0.6;
    const double dec = std::min(1500.0, std::max(20.0, sd_)) / 1000;
    const double dEnv = std::exp(-1 / (dec / 6.9 * sr_));
    const double dDc = std::exp(-1 / (0.002 * sr_));
    // Шаги фаз шести квадратов (ряд 808, разнос — степень отношений к среднему тону).
    const double m = J::hatMean;
    static constexpr double freqs[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
    double dt[6];
    for (int k = 0; k < 6; ++k) dt[k] = std::min(0.45, tune * m * std::pow(freqs[k] / m, spread) / sr2);
    const Svf b1(3440 * tone, 3, sr2), b2(7100 * tone, 3, sr2), hpf(std::min(18000.0, 5500 * tone), 0.7, sr2);
    double q[6];
    for (int k = 0; k < 6; ++k) q[k] = ph_[k];
    double c0 = ic_[0], c1 = ic_[1], c2 = ic_[2], c3 = ic_[3], c4 = ic_[4], c5 = ic_[5];
    double env = std::exp(-t_ / (dec / 6.9 * sr_)), d = dc_;
    int hp = hbPos_;
    double* buf = hb_;
    const int len = J::hbLen, mid = J::hbLen / 2;
    int64_t rn = rnd_;
    double ly = last;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < 2; ++j) {
            double sq = 0;
            for (int k = 0; k < 6; ++k) {
                const double a = q[k];
                sq += (a < 0.5 ? 1.0 : -1.0) + blep(a, dt[k]) - blep(a < 0.5 ? a + 0.5 : a - 0.5, dt[k]);
            }
            const double x = gM * sq / 6 + gN * noiseOf(rn);
            // Два полосовых параллельно, затем ФВЧ.
            double v3 = x - c1;
            double v1 = b1.a1 * c0 + b1.a2 * v3;
            double v2 = c1 + b1.a2 * c0 + b1.a3 * v3;
            c0 = 2 * v1 - c0;
            c1 = 2 * v2 - c1;
            const double bp1 = v1;
            v3 = x - c3;
            v1 = b2.a1 * c2 + b2.a2 * v3;
            v2 = c3 + b2.a2 * c2 + b2.a3 * v3;
            c2 = 2 * v1 - c2;
            c3 = 2 * v2 - c3;
            const double y = bp1 + v1;
            v3 = y - c5;
            v1 = hpf.a1 * c4 + hpf.a2 * v3;
            v2 = c5 + hpf.a2 * c4 + hpf.a3 * v3;
            c4 = 2 * v1 - c4;
            c5 = 2 * v2 - c5;
            const double h = y - hpf.k * v1 - v2;
            const double z = double(float(h));
            buf[hp] = z;
            buf[hp + len] = z;
            hp = hp + 1 == len ? 0 : hp + 1;
            for (int k = 0; k < 6; ++k) {
                q[k] += dt[k];
                if (q[k] >= 1) q[k] -= 1;
            }
        }
        const double acc = half(buf + hp + mid, tables_.hbHK, tables_.hbCK);
        ly = outTanh(out, i, J::hatOut * env * acc, d, dDc);
        env *= dEnv;
    }
    for (int k = 0; k < 6; ++k) ph_[k] = q[k];
    ic_[0] = flush(c0); ic_[1] = flush(c1); ic_[2] = flush(c2);
    ic_[3] = flush(c3); ic_[4] = flush(c4); ic_[5] = flush(c5);
    hbPos_ = hp;
    rnd_ = rn;
    last = ly;
    dc_ = flush(d);
    t_ += double(n);
}

// CLAP: шум → полосовой Q≈2; кластер вспышек и хвост «комнаты».
void JamboDrumVoice::clap(float* out, int n) {
    const Svf fl(J::clapTone(s1_), 2, sr_);
    const double dec = std::min(1200.0, std::max(50.0, sd_)) / 1000;
    const double dC = std::exp(-1 / (0.004 * sr_)), dT = std::exp(-1 / (dec / 6.9 * sr_));
    const double dDc = std::exp(-1 / (0.002 * sr_));
    const double tail = s4_;
    const int nb = bursts_;
    const double lastAt = burstAt_[nb - 1];
    double c1 = ic_[0], c2 = ic_[1], eC = envC_, eT = envT_, d = dc_;
    double tt = t_;
    int64_t rn = rnd_;
    double ly = last;
    for (int i = 0; i < n; ++i) {
        // Начало вспышки — огибающая кластера с 1; после последней — ещё и хвост.
        for (int k = 0; k < nb; ++k) {
            if (tt >= burstAt_[k] && tt < burstAt_[k] + 1) eC = 1;
        }
        if (tt >= lastAt && tt < lastAt + 1) eT = tail;
        const double x = noiseOf(rn);
        const double v3 = x - c2;
        const double v1 = fl.a1 * c1 + fl.a2 * v3;
        const double v2 = c2 + fl.a2 * c1 + fl.a3 * v3;
        c1 = 2 * v1 - c1;
        c2 = 2 * v2 - c2;
        ly = outTanh(out, i, J::clapOut * (eC + eT) * v1, d, dDc);
        eC *= dC;
        eT *= dT;
        tt += 1;
    }
    ic_[0] = flush(c1); ic_[1] = flush(c2);
    envC_ = flush(eC); envT_ = flush(eT);
    rnd_ = rn;
    last = ly;
    dc_ = flush(d);
    t_ = tt;
}

// PERC: шесть затухающих синусов (моды по MATERIAL) + шумовой толчок (HIT), падение высоты (BEND).
void JamboDrumVoice::perc(float* out, int n) {
    const double* sine = tables_.sineD.data();
    const double f = 60 * std::pow(2000.0 / 60, s1_);
    const double dec = std::min(2000.0, std::max(30.0, sd_)) / 1000;
    const double tau0 = dec / 6.9;
    const double semis = 12 * s3_;
    const double dSw = std::exp(-1 / (0.020 * sr_));
    const double hit = s4_;
    const double hitLen = (0.008 - 0.007 * hit) * sr_;
    const double lpK = 1 - std::exp(-2 * kPi * (1000 + 9000 * hit) / sr_);
    const double dDc = std::exp(-1 / (0.002 * sr_));
    // Моды: отношения и уровни по MATERIAL; выше 20 кГц — без звука; затухание короче у высоких.
    double r[6], g[6];
    for (int k = 0; k < 6; ++k) {
        const J::Mode md = J::percModeAt(s2_, hit, k);
        r[k] = md.r;
        g[k] = md.a;
    }
    const double top = 20000 / f;
    for (int k = 1; k < 6; ++k) if (r[k] > top) g[k] = 0;
    const double norm = 1 / std::max(1.0, g[0] + g[1] + g[2] + g[3] + g[4] + g[5]) * 1.6;
    const double tt0 = t_;
    double e[6], kd[6];
    e[0] = g[0] * std::exp(-tt0 / (tau0 * sr_));
    kd[0] = std::exp(-1 / (tau0 * sr_));
    for (int k = 1; k < 6; ++k) {
        e[k] = g[k] * std::exp(-tt0 / (tau0 / std::sqrt(r[k]) * sr_));
        kd[k] = std::exp(-std::sqrt(r[k]) / (tau0 * sr_));
    }
    double p[6];
    for (int k = 0; k < 6; ++k) p[k] = ph_[k];
    double sw = std::exp(-tt0 / (0.020 * sr_)), l = lp_, d = dc_, tt = tt0;
    const double ln2 = 0.057762265;
    int64_t rn = rnd_;
    double ly = last;
    for (int i = 0; i < n; ++i) {
        const double df = f * std::exp(ln2 * semis * sw) / sr_;
        p[0] += df;
        for (int k = 1; k < 6; ++k) p[k] += df * r[k];
        double y = e[0] * sinT(sine, p[0]) + e[1] * sinT(sine, p[1]) + e[2] * sinT(sine, p[2]);
        y += e[3] * sinT(sine, p[3]) + e[4] * sinT(sine, p[4]) + e[5] * sinT(sine, p[5]);
        // Толчок: шум через однополюсный ФНЧ и щелчок.
        if (tt < hitLen) {
            l += (noiseOf(rn) - l) * lpK;
            y += l * (1 - tt / hitLen) * (0.5 + 0.5 * hit);
            if (tt < 0.0003 * sr_) y += 0.5 * hit;
        }
        ly = outTanh(out, i, norm * y, d, dDc);
        for (int k = 0; k < 6; ++k) e[k] *= kd[k];
        sw *= dSw;
        tt += 1;
    }
    for (int k = 0; k < 6; ++k) ph_[k] = p[k] - std::floor(p[k]);
    lp_ = flush(l);
    rnd_ = rn;
    last = ly;
    dc_ = flush(d);
    t_ = tt;
}

}  // namespace jambo
