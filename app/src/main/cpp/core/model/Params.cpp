// Порт Core/Model/Params.swift (ветка swift JAMbo): шкалы, форматы и таблицы ручек.
#include "Params.h"
#include <cmath>
#include "FilterType.h"
#include "ParamsUtil.h"
#include "SynthEngines.h"
#include "../master/MasterChain.h"
#include "../synth/Jambo.h"
#include "../synth/TexGran.h"
#include "../synth/TextureVoice.h"

namespace jambo {
using namespace ParamsUtil;

// MARK: — ParamDef

ParamDef ParamDef::plain() const {
    ParamDef p(id, label, min, max, def, format);
    p.kind = kind;
    p.curve = curve;
    p.exp = exp;
    p.bipolar = bipolar;
    p.options = options;
    p.steps = steps;
    return p;
}

double ParamDef::toNorm(double v) const {
    if (offBottom) {
        const double o = *offBottom;
        return v <= 0 ? 0 : o + (1 - o) * plain().toNorm(std::max(min, v));
    }
    if (autoTop) return v >= MasterChain::autoValue ? 1 : (1 - *autoTop) * plain().toNorm(std::min(max, v));
    if (exp) return std::log(v / min) / std::log(max / min);
    const double lin = std::min(1.0, std::max(0.0, (v - min) / (max - min)));
    // Пункты ступенчатой ручки — от начала хода до конца: первый — 0, последний — 1.
    if (steps) return *steps > 1 ? double(Params::stepIndex(lin, *steps)) / double(*steps - 1) : 0;
    if (curve) return std::pow(lin, 1 / *curve);
    return lin;
}

double ParamDef::fromNorm(double n0) const {
    const double n = std::min(1.0, std::max(0.0, n0));
    if (offBottom) {
        const double o = *offBottom;
        return n < o ? 0 : plain().fromNorm((n - o) / (1 - o));
    }
    if (autoTop) return n >= 1 - *autoTop ? MasterChain::autoValue : plain().fromNorm(n / (1 - *autoTop));
    if (step) return std::round(plain().fromNorm(n) / *step) * *step;
    if (steps) {
        const int s = *steps;
        const int i = s > 1 ? int(std::round(n * double(s - 1))) : 0;
        return min + (double(i) + 0.5) / double(s) * (max - min);
    }
    if (kind == KnobKind::stepped) return std::round(n * (max - min)) + min;
    if (exp) return min * std::pow(max / min, n);
    if (curve) return min + std::pow(n, *curve) * (max - min);
    return min + n * (max - min);
}

// MARK: — Fmt

namespace Fmt {

std::string pct(double v) { return ri(v) + "%"; }

std::string ms(double v) {
    if (v >= 1000) return sf("%.2fs", v / 1000);
    return ri(v) + "ms";
}

std::string hz(double v) {
    if (v >= 1000) return sf("%.1fk", v / 1000);
    if (v >= 100) return ri(v);
    return sf("%.1f", v);
}

double filterHz(double v) { return 20 * std::pow(1000.0, std::min(100.0, std::max(0.0, v)) / 100); }

bool filterOpen(double v, int type) {
    if (type == FilterType::hp) return v <= 0.5;
    if (type == FilterType::bp) return false;
    return v >= 99.5;
}

std::string filter(double v, int type) {
    const int n = int(FilterType::names.size());
    const std::string& name = FilterType::names[size_t(std::min(n - 1, std::max(0, type)))];
    if (filterOpen(v, type) && type != FilterType::ladder) return name + " OFF";
    return name + " " + hz(filterHz(v));
}

std::string pan(double v) {
    const long long r = (long long)std::round(v);
    if (r == 0) return "C";
    return r < 0 ? "L" + std::to_string(-r) : "R" + std::to_string(r);
}

std::string pitch(double v) { return (v > 0 ? "+" : "") + sf("%.1fst", v); }

}  // namespace Fmt

// MARK: — Params

namespace Params {

double volGain(double db) { return db <= volMin + 0.05 ? 0 : std::pow(10.0, db / 20); }

std::string volText(double v) { return v <= volMin + 0.05 ? "−∞\ndB" : sf("%.1f\ndB", v); }

const std::vector<ParamDef>& mixGrid() {
    static const std::vector<ParamDef> g = [] {
        std::vector<ParamDef> v;
        for (int i = 0; i < 16; ++i)
            v.emplace_back("mix.vol" + std::to_string(i + 1), std::to_string(i + 1), volMin, 0, volDef, volText);
        return v;
    }();
    return g;
}

int mixTrack(const std::string& id) {
    if (id.compare(0, 7, "mix.vol") != 0 || id.size() < 8 || id.size() > 9) return -1;
    for (size_t k = 7; k < id.size(); ++k) if (id[k] < '0' || id[k] > '9') return -1;
    const int n = std::stoi(id.substr(7));
    return n >= 1 && n <= 16 ? n - 1 : -1;
}

static const char* const noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string noteName(int n) {
    const int octave = int(std::floor(double(n) / 12)) - 1;
    return std::string(noteNames[((n % 12) + 12) % 12]) + std::to_string(octave);
}

const ParamDef& empty() {
    static const ParamDef p("", "", 0, 1, 0, [](double) { return std::string(); });
    return p;
}

const ParamDef& swing() {
    static const ParamDef p("swing", "Swing", 50, 75, 50, Fmt::pct);
    return p;
}

const ParamDef& rate() {
    static const ParamDef p = ParamDef("rate", "Rate Pitch", -100, 100, 100, Fmt::pct).withBipolar();
    return p;
}
const ParamDef& rateTstr() {
    static const ParamDef p = ParamDef("rate", "Rate Stretch", -100, 100, 100, Fmt::pct).withBipolar();
    return p;
}
const char* const rateModeId = "rate.mode";

std::string stepModeId(const std::string& id) { return id + ".step"; }
const ParamDef& pitchSemi() {
    static const ParamDef p = ParamDef("pitch", "Pitch Semi", -24, 24, 0, Fmt::pitch).withBipolar();
    return p;
}
const ParamDef& startStep() {
    static const ParamDef p("start", "Start Step", 0, 100, 0, Fmt::pct);
    return p;
}
const ParamDef& endStep() {
    static const ParamDef p("end", "End Step", 0, 100, 100, Fmt::pct);
    return p;
}

static ParamDef filterOf(int type, double def) {
    return ParamDef("filter", "Filter", 0, 100, def, [type](double v) { return Fmt::filter(v, type); });
}
const ParamDef& filterLp() { static const ParamDef p = filterOf(FilterType::lp, 100); return p; }
const ParamDef& filterHp() { static const ParamDef p = filterOf(FilterType::hp, 0); return p; }
const ParamDef& filterBp() { static const ParamDef p = filterOf(FilterType::bp, 50); return p; }
const ParamDef& filterLadder() { static const ParamDef p = filterOf(FilterType::ladder, 100); return p; }
const char* const filterTypeId = "filter.type";

const ParamDef& filterDef(int type) {
    if (type == FilterType::hp) return filterHp();
    if (type == FilterType::bp) return filterBp();
    if (type == FilterType::ladder) return filterLadder();
    return filterLp();
}

const std::vector<std::vector<ParamDef>>& trackColumns() {
    static const std::vector<std::vector<ParamDef>> c = {
        {
            smooth("pitch", "Pitch", -24, 24, 0, Fmt::pitch).withBipolar(),
            smooth("start", "Start", 0, 100, 0, Fmt::pct),
            smooth("end", "End", 0, 100, 100, Fmt::pct),
            rate(),
        },
        {
            smooth("attack", "Attack", 0, 2000, 0, Fmt::ms).withCurve(3),
            smooth("decay", "Decay", 0, 4000, 500, Fmt::ms).withCurve(3),
            filterLp(),
            smooth("reso", "Resonance", 0, 100, 0, Fmt::pct),
        },
        {
            smooth("reverb", "Reverb", 0, 100, 0, Fmt::pct),
            smooth("delay", "Delay", 0, 100, 0, Fmt::pct),
            smooth("drive", "Drive", 0, 100, 0, Fmt::pct),
            smooth("sidechain", "Duck", 0, 100, 0, Fmt::pct),
        },
        {
            smooth("pan", "Pan", -64, 63, 0, Fmt::pan).withBipolar(),
            // Velocity (прежний Level, id тот же): на ручке — проценты, хранится 0…127.
            smooth("level", "Velocity", 0, 127, 127, [](double v) { return ri(v / 127 * 100) + "%"; }),
            stepped("ratchet", "Ratchet", {"1", "2", "3", "4"}),
            smooth("prob", "Chance", 0, 100, 100, Fmt::pct),
        },
    };
    return c;
}

const std::vector<ParamDef>& trackGrid() {
    static const std::vector<ParamDef> g = rowsToGrid(trackColumns());
    return g;
}

const std::vector<ParamDef>& synthColumn() {
    static const std::vector<ParamDef> c = {
        smooth("harm", "Harmonics", 0, 100, 50, Fmt::pct),
        smooth("timbre", "Timbre", 0, 100, 50, Fmt::pct),
        smooth("morph", "Morph", 0, 100, 50, Fmt::pct),
        smooth("p4", "Knob 4", 0, 100, 50, Fmt::pct),
    };
    return c;
}

int stepIndex(double n, int steps) {
    return std::min(steps - 1, std::max(0, int(std::floor(n * double(steps)))));
}

/// Свой формат ручки k TEXTURE и GRANULAR (TexGran.format в Swift); пустой — проценты.
static ParamDef::Format texGranFormat(int e, int k) {
    if (e == J::texture && k == 0) return [](double v) { return std::string(TextureMap::colorName(v / 100)); };
    if (e == J::granular && k == 0)
        return [](double v) {
            const double s = TexGran::sourceSeconds;
            return s > 0 ? sf("%.2f s", v / 100 * s) : Fmt::pct(v);
        };
    if (e == J::granular && k == 1) return [](double v) { return Fmt::ms(TexGran::grainSize(v / 100) * 1000); };
    if (e == J::granular && k == 2) return [](double v) { return sf("×%.1f", TexGran::overlap(v / 100)); };
    return nullptr;
}

/// Ручка 1.(k+1) движка e.
static ParamDef synthKnob(const SynthEngine& e, int k, const ParamDef& p) {
    const std::string label = SynthEngines::capitalized(e.knobs[size_t(k)]);
    const std::vector<std::string>& o = e.options[size_t(k)];
    auto own = [&](ParamDef::Format f) { return ParamDef(p.id, label, p.min, p.max, p.def, std::move(f)); };
    if (!o.empty()) {
        const int n = int(o.size());
        return own([o, n](double v) { return o[size_t(stepIndex(v / 100, n))]; })
            .withKind(KnobKind::stepped)
            .withSteps(n);
    }
    // Свой формат: PITCH у KICK — Гц, RATIO у FM — ×2.00, DETUNE у WTABLE — центы.
    if (k == 0 && e.id == J::drum) return own([](double v) { return ri(J::drumPitch(v / 100)) + " Hz"; });
    if (k == 0 && e.id == J::fm) return own([](double v) { return "×" + sf("%.2f", J::fmRatio(v / 100)); });
    // Ударные: высота и TONE — в Гц, SPREAD CLAP — мс, TUNE HAT — в полутонах от 808.
    if (k == 0 && (e.id == J::snare || e.id == J::perc)) {
        const bool snare = e.id == J::snare;
        return own([snare](double v) {
            const double hz = snare ? J::snareTune(v / 100) : J::percTune(v / 100);
            return ri(hz) + " Hz";
        });
    }
    if (k == 0 && e.id == J::hat)
        return own([](double v) {
                   const long long st = (long long)std::round(v / 100 * 24 - 12);
                   return st > 0 ? "+" + std::to_string(st) + " st" : std::to_string(st) + " st";
               })
            .withBipolar();
    if (k == 0 && e.id == J::clap) return own([](double v) { return ri(J::clapTone(v / 100)) + " Hz"; });
    if (k == 2 && e.id == J::clap)
        return own([](double v) {
            const double ms = J::clapGapMin + (J::clapGapMax - J::clapGapMin) * std::pow(v / 100, 1.2);
            return sf("%.1f ms", ms);
        });
    if (auto f = texGranFormat(e.id, k)) return own(std::move(f));
    if (k == 3 && e.id == J::wtable) return own([](double v) { return ri(v / 100 * J::wtDetune) + " ct"; });
    return own(p.format);
}

const std::vector<std::vector<ParamDef>>& synthGrids() {
    static const std::vector<std::vector<ParamDef>> g = [] {
        std::vector<std::vector<ParamDef>> out;
        for (int id = 0; id < SynthEngines::count(); ++id) {
            const SynthEngine* e = SynthEngines::engine(id);
            if (!e) {
                out.emplace_back();
                continue;
            }
            std::vector<ParamDef> grid;
            for (int i = 0; i < 16; ++i) {
                if (i / 4 == 0) {
                    grid.push_back(synthKnob(*e, i % 4, synthColumn()[size_t(i % 4)]));
                    continue;
                }
                const ParamDef& p = trackGrid()[size_t(i)];
                // Свой предел Decay (у KICK — 1000 мс): та же шкала без шага, AUTO и OFF.
                if (p.id == "decay" && e->decayMax) {
                    ParamDef d = p;
                    d.max = *e->decayMax;
                    d.step.reset();
                    d.autoTop.reset();
                    d.offBottom.reset();
                    d.noLock = false;
                    grid.push_back(d);
                    continue;
                }
                grid.push_back(p);
            }
            out.push_back(grid);
        }
        return out;
    }();
    return g;
}

const std::vector<ParamDef>& grid(const std::string& sound) {
    const int e = SynthKey::engine(sound);
    if (e >= 0) return synthGrids()[size_t(e)];
    return trackGrid();
}

}  // namespace Params
}  // namespace jambo
