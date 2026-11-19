#include "DriveTables.h"
#include <cmath>
#include <algorithm>

namespace jambo {

namespace { constexpr double kPi = 3.14159265358979323846; }

const DriveTables& DriveTables::shared() {
    static const DriveTables t;
    return t;
}

DriveTables::DriveTables() {
    // Ядро ФНЧ на Найквисте исходной частоты (окно Кайзера), в отсчётах 4× частоты.
    const int n = firLen;
    std::vector<double> h(n, 0);
    const double c = double(n - 1) / 2;
    const double fc = 0.5 / double(oversample);
    double sum = 0;
    for (int k = 0; k < n; ++k) {
        const double d = double(k) - c;
        const double sinc = d == 0 ? 2 * fc : std::sin(2 * kPi * fc * d) / (kPi * d);
        const double r = d / c;
        const double w = besselI0(kaiserBeta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(kaiserBeta);
        h[k] = sinc * w;
        sum += h[k];
    }
    for (int k = 0; k < n; ++k) h[k] /= sum;
    const int pl = phaseLen;
    phases.assign(4 * pl, 0);
    for (int p = 0; p < 4; ++p) {
        for (int k = p, j = 0; k < n; k += 4, ++j) phases[p * pl + j] = h[k] * 4;
    }
    ph0Gain = h[(n - 1) / 2] * 4;
    decHalf.assign(decC, 0);
    for (int j = 0; j < decC; ++j) decHalf[j] = h[j];
    decMid = h[decC];
    // Антипроизводные.
    const int tn = tableN;
    tanhF.assign(tn + 2, 0); tanhD.assign(tn + 2, 0);
    fuzzF.assign(tn + 2, 0); fuzzD.assign(tn + 2, 0);
    makeTable(tanhF.data(), tanhD.data(), [](double u) { return std::tanh(u); });
    makeTable(fuzzF.data(), fuzzD.data(), [](double u) { return fuzzY(u); });
    tanhMax = tanhF[tn];
    fuzzMax = fuzzF[tn];
    tb = std::tanh(tubeBias);
    td = 1 - tb * tb;
    lcB = tableF(tanhF.data(), tanhD.data(), tanhMax, tubeBias);
    // Компенсация громкости.
    const int cn = compN + 2;
    comp.assign(5 * cn, 0);
    const double tbv = tb, tdv = td;
    const std::function<double(double)> shapes[5] = {
        [](double u) { return std::tanh(u); },
        [](double u) { return u > 1 ? 1.0 : (u < -1 ? -1.0 : u); },
        [tbv, tdv](double u) { return (std::tanh(u + tubeBias) - tbv) / tdv; },
        [](double u) { return fuzzY(u); },
        [](double u) { return std::sin(u); },
    };
    for (int type = 0; type < 5; ++type) makeComp(comp.data() + type * cn, type, shapes[type]);
}

double DriveTables::besselI0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 30; ++k) {
        t *= std::pow(x / (2 * double(k)), 2);
        s += t;
    }
    return s;
}

double DriveTables::fuzzY(double u) { return u < 0 ? -(1 - std::exp(u)) : 1 - std::exp(-u); }

void DriveTables::makeTable(double* F, double* D, const std::function<double(double)>& y) {
    const int n = tableN;
    const double st = tableStep;
    for (int i = 0; i < n + 2; ++i) { F[i] = 0; D[i] = 0; }
    for (int i = 0; i <= n; ++i) {
        D[i] = y(double(i) * st);
        if (i == 0) continue;
        const double a = double(i - 1) * st;
        const int m = 16;
        const double hh = st / double(m);
        double s = y(a) + y(a + st);
        for (int k = 1; k < m; ++k) s += double(k % 2 == 1 ? 4 : 2) * y(a + double(k) * hh);
        F[i] = F[i - 1] + (s * hh) / 3;
    }
    F[n + 1] = F[n];
    D[n + 1] = D[n];
}

double DriveTables::tableF(const double* F, const double* D, double fmax, double u) {
    const double a = u < 0 ? -u : u;
    if (a >= tableMax) return fmax + (a - tableMax);
    const double x = a * 32;
    const int i = int(x);
    const double t = x - double(i), t2 = t * t, t3 = t2 * t;
    const double h = tableStep;
    return (2 * t3 - 3 * t2 + 1) * F[i] + (t3 - 2 * t2 + t) * h * D[i] + (-2 * t3 + 3 * t2) * F[i + 1]
        + (t3 - t2) * h * D[i + 1];
}

void DriveTables::bandLimit(std::vector<double>& v, int H) {
    const int P = int(v.size());
    std::vector<double> cs(P), sn(P);
    for (int k = 0; k < P; ++k) {
        cs[k] = std::cos(2 * kPi * double(k) / double(P));
        sn[k] = std::sin(2 * kPi * double(k) / double(P));
    }
    std::vector<double> a(H + 1, 0), b(H + 1, 0);
    for (int h = 1; h <= H; ++h) {
        double sa = 0, sb = 0;
        int j = 0;
        for (int k = 0; k < P; ++k) {
            sa += v[k] * cs[j];
            sb += v[k] * sn[j];
            j = (j + h) % P;
        }
        a[h] = (2 * sa) / double(P);
        b[h] = (2 * sb) / double(P);
    }
    for (int k = 0; k < P; ++k) {
        double s = 0;
        int j = k;
        for (int h = 1; h <= H; ++h) {
            s += a[h] * cs[j] + b[h] * sn[j];
            j = (j + k) % P;
        }
        v[k] = s;
    }
}

void DriveTables::makeComp(double* t, int type, const std::function<double(double)>& f) {
    const int P = 1024;
    const double rIn = refPeak / 1.4142135623730951;
    std::vector<double> v(P);
    for (int i = 0; i <= compN; ++i) {
        const double g = std::pow(10.0, (double(i) * compStepDb) / 20);
        double mean = 0;
        for (int k = 0; k < P; ++k) {
            v[k] = f(refPeak * g * std::sin(2 * kPi * double(k) / double(P)));
            mean += v[k];
        }
        mean /= double(P);
        double pk = 0, ss = 0;
        for (int k = 0; k < P; ++k) {
            const double d = v[k] - mean;
            pk = std::max(pk, std::fabs(d));
            ss += d * d;
        }
        if (type == 4) {
            bandLimit(v, foldHarm);
            ss = 0;
            for (int k = 0; k < P; ++k) ss += v[k] * v[k];
        }
        t[i] = std::sqrt((pk / refPeak) * (std::sqrt(ss / double(P)) / rIn)) * (type == 4 ? std::pow(10.0, foldTrimDb / 20) : 1);
    }
    t[compN + 1] = t[compN];
}

}  // namespace jambo
