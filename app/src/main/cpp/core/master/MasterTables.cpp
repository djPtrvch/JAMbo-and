// Таблицы мастера (порт Core/Master/MasterTables.swift).
#include "MasterTables.h"
#include <algorithm>
#include <cmath>

namespace jambo {

const MasterTables& MasterTables::shared() {
    static const MasterTables t;
    return t;
}

MasterTables::MasterTables(const DriveTables& drive) : d(drive) {
    const int tn = DriveTables::tableN;
    edgeF.assign(size_t(tn + 2), 0);
    edgeD.assign(size_t(tn + 2), 0);
    DriveTables::makeTable(edgeF.data(), edgeD.data(), [](double u) { return edgeY(u); });
    edgeMax = edgeF[size_t(tn)];
    const int cn = compN + 2;
    comp.assign(size_t(3 * cn), 0);
    const double tb = drive.tb, td = drive.td;
    makeComp(comp.data(), [](double u) { return std::tanh(u); });
    makeComp(comp.data() + cn, [tb, td](double u) { return (std::tanh(u + tubeBias) - tb) / td; });
    makeComp(comp.data() + 2 * cn, [](double u) { return edgeY(u); });
    limPhases.assign(size_t(4 * limTaps), 0);
    makeLimFir(limPhases.data());
}

double MasterTables::satCurve(int type, double driveDb, double x) const {
    const double top = double(compN) * compStepDb;
    const double dB = std::max(0.0, std::min(top, driveDb));
    const double g = std::pow(10.0, dB / 20);
    const double ci = dB / compStepDb;
    const int i0 = std::min(compN, int(std::floor(ci)));
    const double fr = ci - double(i0);
    const int tp = std::max(0, std::min(2, type));
    const size_t base = size_t(tp * (compN + 2));
    const double c = comp[base + size_t(i0)] + (comp[base + size_t(i0) + 1] - comp[base + size_t(i0)]) * fr;
    const double u = x * g;
    double y = edgeY(u);
    if (tp == 0) y = std::tanh(u);
    if (tp == 1) y = (std::tanh(u + tubeBias) - d.tb) / d.td;
    return y / std::max(1e-9, c);
}

void MasterTables::makeComp(double* t, const std::function<double(double)>& f) {
    const int P = 512;
    std::vector<double> v(P);
    for (int i = 0; i <= compN; ++i) {
        const double g = std::pow(10.0, (double(i) * compStepDb) / 20);
        double mean = 0;
        for (int k = 0; k < P; ++k) {
            v[size_t(k)] = f(refPeak * g * std::sin((2 * M_PI * double(k)) / double(P)));
            mean += v[size_t(k)];
        }
        mean /= double(P);
        double pk = 0;
        for (int k = 0; k < P; ++k) pk = std::max(pk, std::abs(v[size_t(k)] - mean));
        t[i] = pk / refPeak;
    }
    t[compN + 1] = t[compN];
}

void MasterTables::makeLimFir(double* out) {
    const int taps = limTaps;
    const int n = 4 * taps;
    const int c = 4 * (taps / 2);
    std::vector<double> h(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double dd = double(k - c);
        const double x = dd / 4;
        const double sinc = x == 0 ? 1 : std::sin(M_PI * x) / (M_PI * x);
        const double r = (M_PI * dd) / double(c + 1);
        const double w = 0.42 + 0.5 * std::cos(r) + 0.08 * std::cos(2 * r);
        h[size_t(k)] = sinc * w;
    }
    for (int p = 0; p < 4; ++p) {
        std::vector<float> f(static_cast<size_t>(taps));
        double sum = 0;
        for (int j = 0; j < taps; ++j) {
            f[size_t(j)] = float(h[size_t(j * 4 + p)]);
            sum += double(f[size_t(j)]);
        }
        for (int j = 0; j < taps; ++j) out[p * taps + j] = double(float(double(f[size_t(j)]) / sum));
    }
}

}  // namespace jambo
