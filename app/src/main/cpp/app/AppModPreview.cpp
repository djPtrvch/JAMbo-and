// App: миниатюры LFO1–3 и ENV в окне звука, пока держат MOD (порт ModPreview.swift, ветка swift JAMbo). LFO — период
// формы с Polarity и Invert и точка на текущей фазе; ENV — форма ADSR по ручкам (условный гейт) и точка на кривой.
#include "App.h"
#include "mod/ModEngine.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace jambo {

namespace {
/// Шаги условного Random на миниатюре.
const double randomSteps[8] = {0.6, -0.3, 0.9, -0.8, 0.2, -0.5, 0.75, -0.1};

double get(const ParamMap& p, const std::string& id, double def) {
    const auto it = p.find(id);
    return it == p.end() ? def : it->second;
}

double lfoValue(const ParamMap& p, int n, double ph) {
    const std::string k = "lfo" + std::to_string(n);
    const double rnd = randomSteps[int(ph * 8) % 8];
    const double s = ModEngine::shape(int(std::lround(get(p, k + ".wave", 0))), ph, rnd);
    const double u = get(p, k + ".polar", 0);
    return (s * (1 - u) + ((s + 1) / 2) * u) * (1 - 2 * get(p, k + ".invert", 0));
}
}  // namespace

ModMini App::modMini(int n) const {
    ModMini m;
    const ParamMap& p = track().params;
    const int t = selected_;
    if (n < 4) {
        const std::string k = "lfo" + std::to_string(n);
        const int w = int(std::lround(get(p, k + ".wave", 0)));
        const std::string name = w >= 0 && w < int(Params::lfoWaves.size()) ? Params::lfoWaves[size_t(w)] : "";
        char hz[32];
        std::snprintf(hz, sizeof hz, "%.2fHz", get(p, k + ".speed", 0));
        m.title = "LFO" + std::to_string(n) + " · " + name + " · " + hz;
        for (int i = 0; i <= 100; ++i) {
            const double ph = std::min(double(i) / 100, 0.9999);
            m.xs.push_back(double(i) / 100);
            m.ys.push_back(lfoValue(p, n, ph));
        }
        const double phase = audio_->modPhase(t * 3 + n - 1);
        m.dotX = phase;
        m.dotY = lfoValue(p, n, phase);
        return m;
    }
    // ENV: значения ADSR — в том же виде, что на ручках.
    const auto& defs = Params::modColumns()[3];
    const char* names[4] = {"A", "D", "S", "R"};
    m.title = "ENV ·";
    for (size_t k = 0; k < defs.size() && k < 4; ++k)
        m.title += std::string(" ") + names[k] + " " + defs[k].format(get(p, defs[k].id, defs[k].def));
    const double a = get(p, "env.attack", 10), d = get(p, "env.decay", 100), sus = get(p, "env.sustain", 40) / 100;
    const double r = get(p, "env.release", 300);
    const double ad = a + d;
    // Форма — только по ручкам: гейт условный (нота любой длины форму не меняет).
    const double gate = ad + std::max(200.0, (ad + r) * 0.3);
    const double total = gate + r;
    std::vector<double> xs;
    for (int i = 0; i <= 100; ++i) xs.push_back(double(i) / 100 * total);
    xs.insert(xs.end(), {a, ad, gate, total});
    std::sort(xs.begin(), xs.end());
    for (double x : xs) {
        if (x < 0 || x > total) continue;
        m.xs.push_back(x / total);
        m.ys.push_back(EnvDot::curve(a, d, sus, r, x, gate));
    }
    const double et = audio_->envOut(t * 2), eg = audio_->envOut(t * 2 + 1);
    const double x = EnvDot::x(a, d, sus, r, et, eg, gate);
    m.dotX = x / total;
    m.dotY = EnvDot::curve(a, d, sus, r, x, gate);
    return m;
}

}  // namespace jambo
