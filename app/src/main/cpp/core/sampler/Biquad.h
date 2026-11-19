// Фильтр голоса — биквад RBJ (порт Core/Sampler/Biquad.swift, ветка swift JAMbo): LP, HP, BP, Q в dB.
// Ручка Filter — частота на всём ходу (Fmt::filterHz), тип — FilterType; открытый LP и HP — LP на 20 кГц.
// Resonance: Q = 0.7 + reso/100·18 (dB).
#pragma once
#include <algorithm>
#include <cmath>
#include "model/FilterType.h"
#include "model/Params.h"

namespace jambo {

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    /// type: −1 LP, 1 HP, 2 BP (3 LADDER — коэффициенты LP); hz — частота среза; qDb — Q в dB.
    void set(int type, double hz, double qDb, double sampleRate) {
        const double f = std::min(hz, sampleRate * 0.5 * 0.999);
        const double w0 = 2 * M_PI * f / sampleRate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / (2 * std::pow(10.0, qDb / 20));
        const double a0 = 1 + alpha;
        a1 = float(-2 * c / a0);
        a2 = float((1 - alpha) / a0);
        if (type == 2) {
            // Bandpass с пиком 0 dB.
            b0 = float(alpha / a0);
            b1 = 0;
            b2 = -b0;
            return;
        }
        if (type == 1) {
            b0 = float((1 + c) / 2 / a0);
            b1 = float(-(1 + c) / a0);
        } else {
            b0 = float((1 - c) / 2 / a0);
            b1 = float((1 - c) / a0);
        }
        b2 = b0;
    }

    struct Knob {
        int type;
        double hz, q;
    };

    /// Ручка Filter, тип фильтра и Resonance → вид биквада (−1 LP, 1 HP, 2 BP, 3 LADDER), частота, Q (dB).
    /// ladder — голос умеет LADDER (SUB); иначе LADDER — LP.
    static Knob knob(double v, double reso, double type, bool ladder) {
        const double q = 0.7 + reso / 100 * 18;
        int ft = FilterType::of(type);
        if (ft == FilterType::ladder && !ladder) ft = FilterType::lp;
        if (ft != FilterType::ladder && Fmt::filterOpen(v, ft)) return {-1, 20000, q};
        const double hz = Fmt::filterHz(v);
        if (ft == FilterType::hp) return {1, hz, q};
        if (ft == FilterType::bp) return {2, hz, q};
        if (ft == FilterType::ladder) return {3, hz, q};
        return {-1, hz, q};
    }

    /// Фильтр звучащего голоса на кусок (k — доля сглаживания): частота плавно идёт к цели. true — скачок (смена
    /// типа или частота дальше, чем в 4 раза).
    static bool glide(int& type, double& hz, int tt, double th, double k) {
        if (tt != type) return true;
        const double r = th / std::max(1.0, hz);
        if (r > 4 || r < 0.25) return true;
        hz += (th - hz) * k;
        return false;
    }

    /// Переход выхода фильтра на сухой сигнал и обратно при скачке (секунды на каждый).
    static constexpr double fadeTime = 0.02;
    /// Уход с HP на сухой сигнал — медленнее.
    static constexpr double fadeHpTime = 0.08;

    /// Доля фильтра в выходе по ходу перехода g (0…1) — S-кривая.
    static float mix(float g) { return g * g * (3 - 2 * g); }
};

}  // namespace jambo
