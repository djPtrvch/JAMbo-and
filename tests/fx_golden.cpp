// Сверка эффектов с эталонами fx_*.i16 (порт Golden/FxSelfTest.swift JAMbo): блоки по 128, k-rate параметры
// в начале блока, вход — как input() в FxSelfTest. Перенесены все: Delay, Reverb, Drive, Duck; остальные эффекты добавляются по мере переноса.
#include <cstdint>
#include "golden.h"
#include "fx/Delay.h"
#include "fx/Reverb.h"
#include "fx/Drive.h"
#include "fx/Duck.h"
#include "fx/FxRandom.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>

using nlohmann::json;
using jambo::FxRandom;

namespace {

void input(const std::string& kind, int frames, std::vector<float>& l, std::vector<float>& r) {
    l.assign(frames, 0);
    r.assign(frames, 0);
    const double sr = 48000;
    if (kind == "burst") {
        FxRandom rnd(7);
        for (int i = 0; i < std::min(2400, frames); ++i) {
            l[i] = float((rnd.next() * 2 - 1) * 0.5);
            r[i] = float((rnd.next() * 2 - 1) * 0.5);
        }
        for (int i = 0; i < 2400 && 36000 + i < frames; ++i) {
            l[36000 + i] = float((rnd.next() * 2 - 1) * 0.25);
            r[36000 + i] = float((rnd.next() * 2 - 1) * 0.25);
        }
    } else if (kind == "sine") {
        for (int i = 0; i < std::min(24000, frames); ++i) {
            const float v = float(0.25 * std::sin(2 * M_PI * 440 * double(i) / sr));
            l[i] = v;
            r[i] = v;
        }
    } else if (kind == "stereo") {
        FxRandom rnd(11);
        for (int i = 0; i < std::min(1440, frames); ++i) {
            l[i] = float((rnd.next() * 2 - 1) * 0.5);
            r[i] = float(0.3 * std::sin(2 * M_PI * 660 * double(i) / sr));
        }
    }
}

void mergeParams(std::map<std::string, double>& vals, const json& p) {
    if (!p.is_object()) return;
    for (auto it = p.begin(); it != p.end(); ++it) vals[it.key()] = it.value().get<double>();
}

/// Рендер случая: L и R. false — эффект ещё не перенесён.
bool render(const json& c, int block, std::vector<float>& outL, std::vector<float>& outR) {
    const std::string proc = c["proc"];
    const int frames = c["frames"];
    const uint32_t seed = c["seed"];
    std::vector<float> iL, iR;
    input(c["input"], frames, iL, iR);
    outL.assign(frames, 0);
    outR.assign(frames, 0);
    std::map<std::string, double> vals;
    if (c.contains("params")) mergeParams(vals, c["params"]);
    // AudioParam — Float32.
    auto p = [&](const char* k, double def) { auto it = vals.find(k); return float(it == vals.end() ? def : it->second); };

    std::unique_ptr<jambo::FxDelay> delay;
    std::unique_ptr<jambo::FxReverb> reverb;
    std::unique_ptr<jambo::FxDrive> drive;
    std::unique_ptr<jambo::FxDuck> duck;
    if (proc == "delay") delay = std::make_unique<jambo::FxDelay>(48000, 6, seed);
    else if (proc == "reverb") reverb = std::make_unique<jambo::FxReverb>(48000, seed);
    else if (proc == "drive") drive = std::make_unique<jambo::FxDrive>(48000);
    else if (proc == "duck") duck = std::make_unique<jambo::FxDuck>(48000);
    else return false;

    std::vector<float> bL(block), bR(block), oL(block), oR(block);
    for (int f = 0; f < frames; f += block) {
        for (const auto& e : c["events"]) {
            if (e["frame"].get<int>() != f) continue;
            if (e.contains("params")) mergeParams(vals, e["params"]);
            if (duck && e.contains("msg")) {
                for (const auto& m : e["msg"]) {
                    if (m["type"] == "flush") duck->flush();
                    if (m["type"] == "trig") {
                        duck->trig(m.value("frame", int64_t(0)), m.value("attack", 0.0), m.value("release", 0.0),
                                   m.value("curve", 50.0));
                    }
                }
            }
        }
        for (int i = 0; i < block; ++i) {
            bL[i] = f + i < frames ? iL[f + i] : 0;
            bR[i] = f + i < frames ? iR[f + i] : 0;
        }
        if (delay) {
            delay->process(bL.data(), bR.data(), oL.data(), oR.data(), block, p("time", 0.25),
                           p("feedback", 0.4), p("filter", 0), p("spread", 0));
        }
        if (reverb) {
            reverb->process(bL.data(), bR.data(), oL.data(), oR.data(), block, p("size", 50), p("decay", 2),
                            p("tone", 0), p("lowcut", 20), p("predelay", 0));
        }
        if (drive) {
            drive->process(bL.data(), bR.data(), oL.data(), oR.data(), block, p("drive", 26), p("type", 0),
                           p("tone", 50), p("lowcut", 20));
        }
        if (duck) {
            duck->process(f, oL.data(), block);
            oR = oL;
        }
        for (int i = 0; i < block && f + i < frames; ++i) {
            outL[f + i] = oL[i];
            outR[f + i] = oR[i];
        }
    }
    golden::quantize(outL);
    golden::quantize(outR);
    return true;
}

}  // namespace

/// Все случаи fx_cases.json, которые уже перенесены. Возвращает результаты (худший канал на случай).
std::vector<golden::Result> runFx() {
    std::ifstream f(golden::dir() + "/fx_cases.json");
    const json all = json::parse(f);
    const int block = all["block"];
    std::vector<golden::Result> out;
    for (const auto& c : all["cases"]) {
        std::vector<float> l, r;
        if (!render(c, block, l, r)) continue;
        const std::string file = c["file"];
        const auto g = golden::load(file);
        std::vector<float> gl(g.size() / 2), gr(g.size() / 2);
        for (size_t i = 0; i < gl.size(); ++i) { gl[i] = g[2 * i]; gr[i] = g[2 * i + 1]; }
        const auto a = golden::compare(file, l, gl);
        const auto b = golden::compare(file, r, gr);
        golden::Result w = a.sampleErrorDb >= b.sampleErrorDb ? a : b;
        w.maxBandDiffDb = std::max(a.maxBandDiffDb, b.maxBandDiffDb);
        w.rmsDb = std::max(a.rmsDb, b.rmsDb);
        w.finite = a.finite && b.finite;
        out.push_back(w);
    }
    return out;
}
