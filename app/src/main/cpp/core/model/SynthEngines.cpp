// Порт Core/Model/SynthEngines.swift (ветка swift JAMbo).
#include "SynthEngines.h"
#include "AnalogMod.h"
#include "KnobDefaults.h"
#include "Track.h"
#include "../synth/Jambo.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace jambo {
namespace SynthEngines {

const std::vector<std::string> knobIds = {"harm", "timbre", "morph", "p4"};
static const std::vector<std::string> waves = {"Sine", "Triangle", "Saw", "Square"};

const std::vector<SynthEngine>& all() {
    static const std::vector<SynthEngine> v = {
        // KICK — бочка (mi:16; высота — ручкой PITCH, монофонична).
        {16, "KICK", "Kick", {"pitch", "sweep", "drive", "click"}, {{}, {}, {}, {}}, true, 1000, {}},
        // ANALOG: два осциллятора A и B; при выборе — синусы без DETUNE, MIX 50 %.
        {17, "ANALOG", "Analog", {"wave A", "wave B", "detune", "mix"}, {waves, waves, {}, {}}, false, std::nullopt,
         {12.5, 12.5, 0, 50}},
        {18, "FM", "FM", {"ratio", "index", "contour", "feedback"}, {{}, {}, {}, {}}, false, std::nullopt, {}},
        // WTABLE: таблица — у дорожки.
        {19, "WTABLE", "Wavetable", {"position", "warp", "unison", "detune"}, {{}, {}, {"1", "2", "3", "4", "5"}, {}},
         false, std::nullopt, {}},
        // Ударные: затухают сами, монофоничны; Decay — длина звука, своя шкала.
        {20, "SNARE", "Snare", {"tune", "snappy", "tone", "punch"}, {{}, {}, {}, {}}, true, 800, {}},
        {21, "HAT", "Hi-hat", {"tune", "spread", "tone", "noise"}, {{}, {}, {}, {}}, true, 1500, {}},
        {22, "CLAP", "Clap", {"tone", "bursts", "spread", "tail"}, {{}, {"1", "2", "3", "4", "5", "6"}, {}, {}}, true,
         1200, {}},
        {23, "PERC", "Percussion", {"tune", "material", "bend", "hit"}, {{}, {}, {}, {}}, true, 2000, {}},
        // SUB: моно-бас; при выборе — треугольник, SUB 40 %, без FAT и GRIT.
        {24, "SUB", "Sub bass", {"wave", "sub", "fat", "grit"}, {{"Triangle", "Saw", "Square"}, {}, {}, {}}, false,
         std::nullopt, {100.0 / 6, 40, 0, 0}},
        // TEXTURE и GRANULAR: при выборе — значения по умолчанию из ТЗ.
        {25, "TEXTURE", "Texture", {"color", "grit", "tone", "metal"}, {{}, {}, {}, {}}, false, std::nullopt,
         {40, 30, 30, 0}},
        {26, "GRANULAR", "Granular", {"position", "size", "density", "spray"}, {{}, {}, {}, {}}, false, std::nullopt,
         {8, 62, 60, 25}},
    };
    return v;
}

const std::vector<int> drums = {16, 20, 21, 22, 23};
const std::vector<int> synths = {17, 24, 18, 19, 25, 26};

int count() {
    static const int n = [] {
        int m = -1;
        for (const auto& e : all()) m = std::max(m, e.id);
        return m + 1;
    }();
    return n;
}

const SynthEngine* engine(int id) {
    static const std::vector<const SynthEngine*> byId = [] {
        std::vector<const SynthEngine*> v(size_t(count()), nullptr);
        for (const auto& e : all()) if (!v[size_t(e.id)]) v[size_t(e.id)] = &e;
        return v;
    }();
    return id >= 0 && id < count() ? byId[size_t(id)] : nullptr;
}

std::string capitalized(const std::string& s) {
    if (s.empty()) return s;
    std::string r = s;
    r[0] = char(std::toupper(static_cast<unsigned char>(r[0])));
    return r;
}

}  // namespace SynthEngines

namespace SynthKey {

std::string key(int engine) { return "mi:" + std::to_string(engine); }

int engine(const std::string& key) {
    if (key.size() < 4 || key.compare(0, 3, "mi:") != 0) return -1;
    // Int(String) Swift: только цифры с необязательным знаком, без пробелов.
    const std::string t = key.substr(3);
    size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
    if (i == t.size() || t.size() - i > 9) return -1;
    for (size_t k = i; k < t.size(); ++k) if (!std::isdigit(static_cast<unsigned char>(t[k]))) return -1;
    const int n = std::atoi(t.c_str());
    return SynthEngines::engine(n) ? n : -1;
}

}  // namespace SynthKey
void Track::setSynth(int engine, const KnobDefaults* defaults) {
    const std::string key = SynthKey::key(engine);
    const std::string was = sample;
    sample = key;
    const SynthEngine* e = SynthEngines::engine(engine);
    if (!e) return;
    if (e->initKnobs.size() == 4 && was != key)
        for (size_t k = 0; k < 4; ++k) params[SynthEngines::knobIds[k]] = e->initKnobs[k];
    // ANALOG с другого звука: модуляция и октава B выключены.
    if (was != key && engine == J::analog) {
        params[AnalogMod::modId] = 0;
        params[AnalogMod::octId] = 0;
    }
    if (was != key && defaults) defaults->applySound(*this);
    if (e->decayMax) {
        const auto it = params.find("decay");
        if (it != params.end() && it->second > *e->decayMax) it->second = *e->decayMax;
    }
}

}  // namespace jambo
