// Ударные SNARE, HAT, CLAP, PERC — проверка по свойствам (порт DrumEngineTests.swift JAMbo): NaN, пик, слышимость,
// затухание, стык ретрига, вспышки CLAP, алиасинг HAT. Эталонов у этих движков нет.
#include "synth/JamboDrums.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace jambo;

namespace {

std::vector<float> render(int e, const double k[4], double decay = 400, int frames = 24000, int retrig = -1) {
    JamboDrumVoice v(JamboTables::shared(), 48000);
    v.trigger(e, k[0], k[1], k[2], k[3], decay);
    std::vector<float> out(frames, 0);
    int pos = 0;
    while (pos < frames) {
        if (pos == retrig) v.trigger(e, k[0], k[1], k[2], k[3], decay);
        int n = std::min(24, frames - pos);
        if (retrig >= 0 && pos < retrig) n = std::min(n, retrig - pos);
        v.render(out.data() + pos, n);
        pos += n;
    }
    return out;
}

float peakOf(const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX) {
    float p = 0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) p = std::max(p, std::fabs(x[i]));
    return p;
}

void fft(std::vector<double>& re, std::vector<double>& im) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double a = -2 * M_PI / double(len);
        for (size_t s = 0; s < n; s += len) {
            for (size_t k = 0; k < len / 2; ++k) {
                const double wr = std::cos(a * k), wi = std::sin(a * k);
                const size_t p = s + k, q = p + len / 2;
                const double xr = re[q] * wr - im[q] * wi, xi = re[q] * wi + im[q] * wr;
                re[q] = re[p] - xr; im[q] = im[p] - xi;
                re[p] += xr; im[p] += xi;
            }
        }
    }
}

int fails = 0;
void check(bool ok, const std::string& what) {
    std::printf("%-58s %s\n", what.c_str(), ok ? "OK" : "FAIL");
    if (!ok) ++fails;
}

}  // namespace

int runDrums() {
    fails = 0;
    const int drums[4] = {J::snare, J::hat, J::clap, J::perc};
    const char* names[4] = {"SNARE", "HAT", "CLAP", "PERC"};
    const double def[4] = {0.5, 0.5, 0.5, 0.5};
    for (int di = 0; di < 4; ++di) {
        const int e = drums[di];
        // Сетка ручек и Decay: NaN нет, пик < 0 dBFS.
        bool finite = true, below = true;
        for (double a : {0.0, 0.5, 1.0}) for (double b : {0.0, 0.5, 1.0}) for (double dec : {10.0, 400.0, 2000.0}) {
            const double k[4] = {a, b, 1 - a, b};
            const auto x = render(e, k, dec, 12000);
            for (float v : x) if (!std::isfinite(v)) finite = false;
            if (peakOf(x) >= 1) below = false;
        }
        check(finite && below, std::string(names[di]) + ": grid 0/50/100 %, no NaN, peak < 0 dBFS");
        const float pk = peakOf(render(e, def));
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s: audible by default (peak %.1f dBFS)", names[di], 20 * std::log10(pk));
        check(pk > 0.2f, buf);
        // Decay — длина звука: через Decay уровень ниже −50 dB от пика.
        const auto x = render(e, def, 300, 48000);
        check(peakOf(x, 14400 + 4800) / peakOf(x) < 0.0032f, std::string(names[di]) + ": decays below -50 dB after Decay");
    }
    // Ретриг без щелчка: шаг между отсчётами на стыке — не больше, чем в начале удара.
    for (int e : {J::snare, J::perc}) {
        const double k[4] = {0.5, 0.5, 0.5, 0};
        const auto fresh = render(e, k, 400, 3000);
        float steady = 0;
        for (int i = 1; i < 3000; ++i) steady = std::max(steady, std::fabs(fresh[i] - fresh[i - 1]));
        const auto x = render(e, k, 400, 9000, 6000);
        float worst = 0;
        for (int i = 5990; i < 6100; ++i) worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
        check(worst <= steady * 1.2f + 1e-4f, std::string(e == J::snare ? "SNARE" : "PERC") + ": retrig without click");
    }
    // CLAP: вспышки по BURSTS — число пиков огибающей в кластере.
    for (auto [knob, count] : {std::pair<double, int>{0.05, 1}, {0.5, 4}, {0.95, 6}}) {
        const double k[4] = {0.5, knob, 0.5, 0};
        const auto x = render(J::clap, k, 50, 9600);
        std::vector<float> env;
        for (int b = 0; b < 9600 / 96; ++b) env.push_back(peakOf(x, b * 96, b * 96 + 96));
        float pk = 0;
        for (float v : env) pk = std::max(pk, v);
        int rises = 0;
        for (size_t b = 1; b < env.size(); ++b) if (env[b] > 3 * env[b - 1] && env[b] > pk / 3) ++rises;
        check(rises + 1 == count, "CLAP: BURSTS " + std::to_string(count));
    }
    // Алиасинг HAT (только металл): не на нечётных гармониках шести квадратов — ниже −50 dB.
    {
        const double k[4] = {0.75, 0.5, 0.75, 0};
        const auto x = render(J::hat, k, 1500, 16384 + 480);
        const int n = 16384;
        std::vector<double> re(n), im(n, 0);
        for (int i = 0; i < n; ++i) {
            const double a = 2 * M_PI * i / n;
            re[i] = x[480 + i] * (0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a) - 0.01168 * std::cos(3 * a));
        }
        fft(re, im);
        const double tune = std::pow(2.0, 2 * 0.75 - 1), m = J::hatMean, binHz = 48000.0 / n;
        std::set<int> expected;
        for (double f0 : {205.3, 304.4, 369.6, 522.7, 540.0, 800.0}) {
            const double f = tune * m * (f0 / m);
            for (double kk = 1; kk * f < 24000; kk += 2) {
                const int b = int(std::round(kk * f / binHz));
                for (int d = -6; d <= 6; ++d) expected.insert(b + d);
            }
        }
        double top = 0, worst = 0;
        for (int b = int(20 / binHz); b < int(20000 / binHz); ++b) {
            const double mag = std::sqrt(re[b] * re[b] + im[b] * im[b]);
            if (expected.count(b)) top = std::max(top, mag); else worst = std::max(worst, mag);
        }
        const double db = 20 * std::log10(std::max(worst, 1e-12) / std::max(top, 1e-12));
        char buf[96];
        std::snprintf(buf, sizeof buf, "HAT: aliasing %.1f dB (< -50)", db);
        check(db < -50, buf);
    }
    return fails;
}
