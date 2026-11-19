#include "golden.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace golden {

std::string dir() { return GOLDEN_DIR; }

std::vector<float> load(const std::string& file) {
    std::ifstream f(dir() + "/" + file, std::ios::binary);
    if (!f) throw std::runtime_error("no golden file: " + file);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<float> out(bytes.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        int16_t v;
        std::memcpy(&v, &bytes[2 * i], 2);
        out[i] = float(v) / 32767.0f;
    }
    return out;
}

void quantize(std::vector<float>& x) {
    for (float& v : x) v = std::round(std::min(std::max(v, -1.0f), 1.0f) * 32767.0f) / 32767.0f;
}

namespace {
void fft(std::vector<double>& re, std::vector<double>& im) {
    const size_t n = re.size();
    if (n <= 1) return;
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j |= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    const size_t half = n / 2;
    std::vector<double> cosT(half), sinT(half);
    for (size_t k = 0; k < half; ++k) {
        const double ang = -2 * M_PI * double(k) / double(n);
        cosT[k] = std::cos(ang);
        sinT[k] = std::sin(ang);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t step = n / len;
        for (size_t start = 0; start < n; start += len) {
            for (size_t k = 0; k < len / 2; ++k) {
                const double wr = cosT[k * step], wi = sinT[k * step];
                const size_t a = start + k, b = a + len / 2;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
        }
    }
}

/// Энергия третьоктавных полос (центры 1000·2^(k/3), k = −17…13) по БПФ всего сигнала (48 кГц).
std::vector<double> bands(const std::vector<float>& x) {
    size_t size = 1;
    while (size < x.size()) size <<= 1;
    std::vector<double> re(size, 0), im(size, 0);
    for (size_t i = 0; i < x.size(); ++i) re[i] = x[i];
    fft(re, im);
    const double binHz = 48000.0 / double(size);
    std::vector<double> out;
    for (int k = -17; k <= 13; ++k) {
        const double center = 1000 * std::pow(2.0, k / 3.0);
        const double lo = center * std::pow(2.0, -1.0 / 6), hi = center * std::pow(2.0, 1.0 / 6);
        double e = 0;
        size_t bin = std::max<size_t>(1, size_t(std::ceil(lo / binHz)));
        while (double(bin) * binHz < hi && bin <= size / 2) {
            e += re[bin] * re[bin] + im[bin] * im[bin];
            ++bin;
        }
        out.push_back(e);
    }
    return out;
}
}  // namespace

Result compare(const std::string& file, const std::vector<float>& actual, const std::vector<float>& golden) {
    const size_t n = std::min(actual.size(), golden.size());
    double err = 0, ref = 0;
    for (size_t i = 0; i < n; ++i) {
        const double d = double(actual[i]) - double(golden[i]);
        err += d * d;
        ref += double(golden[i]) * double(golden[i]);
    }
    Result r;
    r.file = file;
    r.sampleErrorDb = 10 * std::log10(std::max(err, 1e-30) / std::max(ref, 1e-30));
    r.rmsDb = 10 * std::log10(std::max(ref / double(std::max<size_t>(n, 1)), 1e-30));
    const auto a = bands(actual), g = bands(golden);
    double total = 0;
    for (double v : g) total += v;
    for (size_t k = 0; k < g.size(); ++k) {
        if (std::max(a[k], g[k]) <= total * 1e-8) continue;
        r.maxBandDiffDb = std::max(r.maxBandDiffDb,
                                   std::fabs(10 * std::log10(std::max(a[k], 1e-30) / std::max(g[k], 1e-30))));
    }
    r.finite = actual.size() == golden.size();
    for (float v : actual) if (!std::isfinite(v)) r.finite = false;
    return r;
}

}  // namespace golden
