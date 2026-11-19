#include <cstdint>
#include "Wavetable.h"
#include <cmath>
#include <algorithm>

namespace jambo {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double lerp(double a, double b, double u) { return a + (b - a) * u; }
/// Округление: половина — вверх.
inline int jsRound(double x) { return int(std::floor(x + 0.5)); }
}  // namespace

const char* const WtTable::builtins[6] = {"BASIC", "PWM", "ORGAN", "FORMANT", "DIGITAL", "METAL"};

WtTable::WtTable(int f) : frames(f) {
    data_.resize(levelCount);
    for (int l = 0; l < levelCount; ++l) {
        const int h = 1024 >> l;
        const int size = std::min(tableSize, std::max(256, 4 * h));
        sizes_[l] = size;
        data_[l].assign(size_t(frames) * size, 0.0f);
        ptrs_[l] = data_[l].data();
    }
}

std::unique_ptr<WtTable> WtTable::builtin(const std::string& name) {
    return buildMips(builtinSpectra(name), true);
}

void WtTable::fft(std::vector<double>& re, std::vector<double>& im, bool inverse) {
    const int n = int(re.size());
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    const double sign = inverse ? 1 : -1;
    for (int len = 2; len <= n; len <<= 1) {
        const double a = sign * 2 * kPi / double(len);
        const double wr0 = std::cos(a), wi0 = std::sin(a);
        for (int i = 0; i < n; i += len) {
            double wr = 1, wi = 0;
            for (int k = 0; k < len / 2; ++k) {
                const int p = i + k, q = p + len / 2;
                const double xr = re[q] * wr - im[q] * wi, xi = re[q] * wi + im[q] * wr;
                re[q] = re[p] - xr;
                im[q] = im[p] - xi;
                re[p] += xr;
                im[p] += xi;
                const double t = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0;
                wr = t;
            }
        }
    }
}

std::unique_ptr<WtTable> WtTable::buildMips(const Spectra& sp, bool perFrame) {
    auto table = std::make_unique<WtTable>(sp.frames);
    std::vector<double> peaks(sp.frames, 0);
    for (int l = 0; l < levelCount; ++l) {
        const int h = 1024 >> l;
        const int size = table->sizes_[l];
        const int lim = std::min({h, size / 2 - 1, harmonics});
        float* out = table->data_[l].data();
        std::vector<double> re(size), im(size);
        for (int f = 0; f < sp.frames; ++f) {
            std::fill(re.begin(), re.end(), 0.0);
            std::fill(im.begin(), im.end(), 0.0);
            for (int n = 1; n <= lim; ++n) {
                re[n] = sp.re[f][n];
                im[n] = sp.im[f][n];
                re[size - n] = sp.re[f][n];
                im[size - n] = -sp.im[f][n];
            }
            fft(re, im, true);
            for (int k = 0; k < size; ++k) {
                out[size_t(f) * size + k] = float(re[k]);
                if (l == 0) peaks[f] = std::max(peaks[f], std::fabs(re[k]));
            }
        }
    }
    const double all = peaks.empty() ? 0 : *std::max_element(peaks.begin(), peaks.end());
    for (int l = 0; l < levelCount; ++l) {
        const int size = table->sizes_[l];
        float* out = table->data_[l].data();
        for (int f = 0; f < sp.frames; ++f) {
            const double pk = perFrame ? peaks[f] : all;
            const double g = pk > 0 ? 1 / pk : 1;
            for (size_t k = size_t(f) * size; k < size_t(f + 1) * size; ++k) out[k] = float(double(out[k]) * g);
        }
    }
    return table;
}

WtTable::Spectra WtTable::spectraFrom(int frames, int limit, const std::function<double(double, int)>& amp,
                                      const std::function<double(double, int)>& phase) {
    Spectra sp{frames, {}, {}};
    for (int f = 0; f < frames; ++f) {
        const double t = frames > 1 ? double(f) / double(frames - 1) : 0;
        std::vector<double> r(limit + 1, 0), i(limit + 1, 0);
        for (int n = 1; n <= limit; ++n) {
            const double a = amp(t, n);
            if (a == 0) continue;
            const double ph = phase(t, n) - kPi / 2;
            r[n] = a / 2 * std::cos(ph);
            i[n] = a / 2 * std::sin(ph);
        }
        sp.re.push_back(std::move(r));
        sp.im.push_back(std::move(i));
    }
    return sp;
}

WtTable::Spectra WtTable::builtinSpectra(const std::string& name, int fr, int lim) {
    const auto zero = [](double, int) { return 0.0; };
    if (name == "PWM") {
        // Импульс: ширина 50 % → 5 %.
        return spectraFrom(fr, lim, [](double t, int n) {
            return (2 / (double(n) * kPi)) * std::sin(double(n) * kPi * lerp(0.5, 0.05, t));
        }, [](double, int) { return kPi / 2; });
    }
    if (name == "ORGAN") {
        // Дроубары (гармоники 1, 2, 3, 4, 5, 6, 8, 10, 12, 16): регистровки сменяют друг друга.
        static const int bars[10] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16};
        static const double regs[6][10] = {
            {8, 8, 0, 0, 0, 0, 0, 0, 0, 0}, {8, 8, 8, 0, 0, 0, 0, 0, 0, 0}, {8, 6, 8, 4, 0, 3, 0, 0, 0, 0},
            {8, 8, 8, 8, 0, 0, 0, 0, 0, 8}, {6, 8, 8, 6, 4, 4, 3, 2, 2, 4}, {8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
        };
        return spectraFrom(fr, lim, [](double t, int n) {
            int k = -1;
            for (int b = 0; b < 10; ++b) if (bars[b] == n) { k = b; break; }
            if (k < 0) return 0.0;
            const double x = t * double(6 - 1);
            const int i = std::min(6 - 2, int(std::floor(x)));
            return lerp(regs[i][k], regs[i + 1][k], x - double(i)) / 8 / std::sqrt(double(n));
        }, zero);
    }
    if (name == "FORMANT") {
        // Гласные A, E, I, O, U аддитивно: огибающая трёх формант (тон таблицы ~110 Гц).
        static const double v[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {350, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};
        static const double w[3] = {1, 0.6, 0.3};
        return spectraFrom(fr, lim, [](double t, int n) {
            const double x = t * double(5 - 1);
            const int i = std::min(5 - 2, int(std::floor(x)));
            const double u = x - double(i);
            const double hz = double(n) * 110;
            if (hz > 6000) return 0.0;
            double a = 0;
            for (int k = 0; k < 3; ++k) {
                const double fc = lerp(v[i][k], v[i + 1][k], u), bw = 60 + fc * 0.08;
                a += w[k] * std::exp(-0.5 * std::pow((hz - fc) / bw, 2));
            }
            return a;
        }, zero);
    }
    if (name == "DIGITAL") {
        // Наборы гармоник: число гармоник растёт с кадром, фазы случайные — «цифровой» тембр.
        std::vector<double> ph(harmonics + 1);
        uint64_t s = 7;
        for (int k = 0; k <= harmonics; ++k) {
            s = (s * 1664525ULL + 1013904223ULL) & 0xFFFFFFFFULL;
            ph[k] = double(s) / 4294967296.0 * kPi * 2;
        }
        return spectraFrom(fr, lim, [](double t, int n) {
            const int top = 1 + int(std::floor(t * 63));
            if (n > top * 4) return 0.0;
            return (n & (1 + int(std::floor(t * 7)))) != 0 || n == 1 ? 1 / std::sqrt(double(n)) : 0.0;
        }, [ph](double, int n) { return ph[n]; });
    }
    if (name == "METAL") {
        // Инхармонические партиалы (моды стержня), растянутые вверх с кадром — металлический тон.
        static const double modes[6] = {1, 2.756, 5.404, 8.933, 13.344, 18.64};
        return spectraFrom(fr, lim, [](double t, int n) {
            const double base = lerp(2, 14, t);
            double a = 0;
            for (int k = 0; k < 6; ++k) if (jsRound(modes[k] * base) == n) a += 1 / double(k + 1);
            return a + (n == 1 ? 0.3 : 0);
        }, zero);
    }
    // BASIC: синус → пила (первая половина кадров) → квадрат (вторая).
    return spectraFrom(fr, lim, [](double t, int n) {
        const double saw = 1 / double(n), sq = n % 2 == 1 ? 1 / double(n) : 0;
        if (t < 0.5) {
            const double u = t * 2;
            return (n == 1 ? 1 - u : 0) + u * saw;
        }
        const double u = (t - 0.5) * 2;
        return (1 - u) * saw + u * sq;
    }, zero);
}

}  // namespace jambo
