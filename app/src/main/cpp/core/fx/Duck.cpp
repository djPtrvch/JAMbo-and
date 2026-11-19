#include <cstdint>
#include "Duck.h"
#include <cmath>
#include <algorithm>

namespace jambo {

namespace {
constexpr double kMinAttackMs = 1;
constexpr double kQBase = 2;
constexpr double kQRange = 4;
constexpr int kReportFrames = 512;
constexpr double kReportFloor = 0.001;
/// Округление: половина — вверх.
inline double jsRound(double x) { return std::floor(x + 0.5); }
}  // namespace

void FxDuck::trig(int64_t frame, double attack, double release, double curve) {
    if (count_ >= maxQueue) removeFirst();
    int k = count_;
    while (k > 0 && qFrame_[k - 1] > frame) {
        qFrame_[k] = qFrame_[k - 1];
        qAttack_[k] = qAttack_[k - 1];
        qRelease_[k] = qRelease_[k - 1];
        qCurve_[k] = qCurve_[k - 1];
        --k;
    }
    qFrame_[k] = frame;
    qAttack_[k] = std::isfinite(attack) ? attack : 0;
    qRelease_[k] = std::isfinite(release) ? release : 0;
    qCurve_[k] = std::isfinite(curve) ? curve : 50;
    ++count_;
}

void FxDuck::removeFirst() {
    for (int k = 1; k < count_; ++k) {
        qFrame_[k - 1] = qFrame_[k];
        qAttack_[k - 1] = qAttack_[k];
        qRelease_[k - 1] = qRelease_[k];
        qCurve_[k - 1] = qCurve_[k];
    }
    --count_;
}

void FxDuck::start(double attack, double release, double curve) {
    stage_ = 1;
    pos_ = 0;
    e0_ = e_;
    A_ = std::max(1.0, jsRound((std::max(kMinAttackMs, attack) / 1000) * sr_));
    R_ = std::max(1.0, jsRound((std::max(0.0, release) / 1000) * sr_));
    const double c = std::min(100.0, std::max(0.0, curve));
    q_ = kQBase * std::pow(kQRange, (50 - c) / 50);
}

void FxDuck::process(int64_t f0, float* out, int n) {
    if (stage_ == 0 && (count_ == 0 || qFrame_[0] >= f0 + n)) {
        for (int i = 0; i < n; ++i) out[i] = 0;
        return;
    }
    for (int i = 0; i < n; ++i) {
        // Триги этого отсчёта (опоздавшие — в первом отсчёте блока).
        while (count_ > 0 && qFrame_[0] <= f0 + i) {
            const double a = qAttack_[0], r = qRelease_[0], c = qCurve_[0];
            removeFirst();
            start(a, r, c);
        }
        double v = e_;
        if (stage_ == 1) {
            pos_ += 1;
            v = e0_ + (1 - e0_) * std::min(1.0, pos_ / A_);
            if (pos_ >= A_) { stage_ = 2; pos_ = 0; v = 1; }
        } else if (stage_ == 2) {
            pos_ += 1;
            const double u = pos_ / R_;
            if (u >= 1) { v = 0; stage_ = 0; }
            else v = std::pow(1 - u, q_);
        }
        if (!(v >= 0)) v = 0; else if (v > 1) v = 1;
        e_ = v;
        out[i] = float(v);
    }
    report(n);
}

void FxDuck::report(int n) {
    sinceReport_ += n;
    const bool due = e_ > kReportFloor ? sinceReport_ >= kReportFrames : reported_ > kReportFloor;
    if (due) {
        sinceReport_ = 0;
        reported_ = e_;
        meter_ = e_ > kReportFloor ? e_ : 0;
    }
}

}  // namespace jambo
