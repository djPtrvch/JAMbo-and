#include "JamboTables.h"
#include "Jambo.h"
#include <cmath>
#include <stdexcept>

namespace jambo {

namespace { constexpr double kPi = 3.14159265358979323846; }

const JamboTables& JamboTables::shared() {
    static const JamboTables t;
    return t;
}

JamboTables::JamboTables() {
    const double tau = kPi * 2;
    sineD.resize(J::sinN + 1);
    for (int i = 0; i <= J::sinN; ++i) sineD[i] = double(float(std::sin(tau * double(i) / double(J::sinN))));

    // Коэффициенты: sinc · окно, нормировка по сумме; только центр и нечётные отводы (чётные — нули).
    const int len = J::hbLen;
    const int c = (len - 1) / 2;
    std::vector<double> all(len, 0);
    double sum = 0;
    for (int n = 0; n < len; ++n) {
        const double k = double(n - c);
        const double sinc = k == 0 ? 0.5 : std::sin(kPi * k / 2) / (kPi * k);
        const double x = double(n) / double(len - 1);
        const double w = 0.35875 - 0.48829 * std::cos(tau * x) + 0.14128 * std::cos(2 * tau * x) - 0.01168 * std::cos(3 * tau * x);
        all[n] = sinc * w;
        sum += all[n];
    }
    std::vector<int> idx;
    std::vector<float> kk;
    for (int n = 0; n < len; ++n) {
        if ((n - c) % 2 == 0) continue;
        idx.push_back(n);
        kk.push_back(float(all[n] / sum));
    }
    idx.push_back(c);
    kk.push_back(float(all[c] / sum));
    const int hbN = int(idx.size());
    if (len != 63 || hbN != 33) throw std::logic_error("half-band unrolled for 63 taps");
    for (int i = 0; i < hbN - 1; ++i) {
        if (idx[i] < c) hbHK[(c - idx[i] - 1) / 2] = double(kk[i]);
    }
    hbCK = double(kk[hbN - 1]);

    // Пик смеси соседних форм (кроссфейд равной мощности, нормированный по сумме амплитуд) — по сетке.
    auto shape = [&](int k, double ph) -> double {
        if (k == 0) return std::sin(tau * ph);
        if (k == 1) return 1 - 4 * std::fabs(ph < 0.75 ? ph - 0.25 : ph - 1.25);
        if (k == 2) return 1 - 2 * ph;
        return ph < 0.5 ? 1 : -1;
    };
    const int steps = J::waveSteps;
    waveGain.resize(3 * (steps + 1));
    for (int seg = 0; seg < 3; ++seg) {
        for (int st = 0; st <= steps; ++st) {
            const double wf = double(st) / double(steps);
            const double ca = std::cos(wf * kPi / 2), cb = std::sin(wf * kPi / 2);
            double pk = 0;
            for (int i = 0; i < 2048; ++i) {
                const double ph = double(i) / 2048;
                pk = std::max(pk, std::fabs((ca * shape(seg, ph) + cb * shape(seg + 1, ph)) / (ca + cb)));
            }
            waveGain[seg * (steps + 1) + st] = float(1 / pk);
        }
    }
    shapeGain[0] = waveGain[0];
    shapeGain[1] = waveGain[steps + 1];
    shapeGain[2] = waveGain[2 * (steps + 1)];
    shapeGain[3] = waveGain[3 * (steps + 1) - 1];
}

}  // namespace jambo
