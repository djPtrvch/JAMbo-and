// Сверка мастера с эталонами mst_*.i16 (порт Golden/MasterSelfTest.swift JAMbo): блоки по 128, параметры — сообщения в
// начале блока; модули по отдельности и цепочка целиком.
#include "golden.h"
#include "master/Clipper.h"
#include "master/Glue.h"
#include "master/Limiter.h"
#include "master/MasterChain.h"
#include "master/Saturator.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>

using nlohmann::json;
using namespace jambo;

namespace {
using I = MasterChain::Index;
const std::map<std::string, std::map<std::string, int>>& keys() {
    static const std::map<std::string, std::map<std::string, int>> k = {
        {"sat", {{"drive", I::satDrive}, {"type", I::satType}, {"tone", I::satTone}, {"mix", I::satMix},
                 {"focus", I::satFocus}}},
        {"glue", {{"thr", I::cmpThr}, {"att", I::cmpAtt}, {"rel", I::cmpRel}, {"auto", I::cmpAuto},
                  {"makeup", I::cmpMakeup}, {"ratio", I::cmpRatio}, {"knee", I::cmpKnee}, {"mix", I::cmpMix},
                  {"schpf", I::cmpSchpf}, {"bypass", I::cmpBypass}}},
        {"clip", {{"drive", I::clipDrive}, {"ceiling", I::clipCeiling}, {"softness", I::clipSoftness},
                  {"bass", I::clipBass}, {"bypass", I::clipBypass}}},
        {"lim", {{"gain", I::limGain}, {"ceiling", I::limCeiling}, {"lookahead", I::limLookahead}, {"rel", I::limRel},
                 {"auto", I::limAuto}, {"bypass", I::limBypass}}},
    };
    return k;
}
void apply(const json& msg, const std::string& module, double* p) {
    auto m = keys().find(module);
    if (m == keys().end() || !msg.is_object()) return;
    for (auto it = msg.begin(); it != msg.end(); ++it) {
        auto k = m->second.find(it.key());
        if (k == m->second.end()) continue;
        if (it.value().is_boolean())
            p[k->second] = it.value().get<bool>() ? 1 : 0;
        else if (it.value().is_number())
            p[k->second] = it.value().get<double>();
    }
}
}  // namespace

std::vector<golden::Result> runMaster() {
    std::ifstream f(golden::dir() + "/mst_cases.json");
    const json all = json::parse(f);
    const int block = all.contains("block") ? all["block"].get<int>() : 128;
    const std::map<std::string, std::string> modules = {
        {"saturator", "sat"}, {"glue", "glue"}, {"clipper", "clip"}, {"limiter", "lim"}};
    std::vector<golden::Result> out;
    for (const auto& c : all["cases"]) {
        const std::string proc = c["proc"];
        const int frames = c["frames"];
        std::vector<std::pair<int, json>> msgs{{0, c["msg"]}};
        if (c.contains("events"))
            for (const auto& e : c["events"]) msgs.push_back({e["frame"].get<int>(), e["msg"]});
        std::vector<float> iL, iR;
        golden::masterInput(c["input"], frames, iL, iR);
        std::vector<float> outL(static_cast<size_t>(frames)), outR(static_cast<size_t>(frames)), bL(static_cast<size_t>(block)),
            bR(static_cast<size_t>(block));
        double p[I::count];
        for (int k = 0; k < I::count; ++k) p[k] = MasterChain::defaults[k];
        MasterChain chain(48000);
        for (int fr = 0; fr < frames; fr += block) {
            for (const auto& m : msgs) {
                if (m.first != fr) continue;
                if (proc == "chain") {
                    for (const char* mod : {"sat", "glue", "clip", "lim"})
                        if (m.second.contains(mod)) apply(m.second[mod], mod, p);
                } else {
                    auto mm = modules.find(proc);
                    if (mm != modules.end()) apply(m.second, mm->second, p);
                }
            }
            for (int i = 0; i < block; ++i) {
                bL[size_t(i)] = fr + i < frames ? iL[size_t(fr + i)] : 0;
                bR[size_t(i)] = fr + i < frames ? iR[size_t(fr + i)] : 0;
            }
            float* l = bL.data();
            float* r = bR.data();
            if (proc == "saturator")
                chain.sat->process(l, r, l, r, block, p[I::satDrive], p[I::satType], p[I::satTone], p[I::satMix],
                                   p[I::satFocus]);
            else if (proc == "glue")
                chain.glue->process(l, r, l, r, block, p[I::cmpThr], p[I::cmpAtt], p[I::cmpRel], p[I::cmpAuto] >= 0.5,
                                    p[I::cmpMakeup], p[I::cmpRatio], p[I::cmpKnee], p[I::cmpMix], p[I::cmpSchpf],
                                    p[I::cmpBypass]);
            else if (proc == "clipper")
                chain.clip->process(l, r, l, r, block, p[I::clipDrive], p[I::clipCeiling], p[I::clipSoftness],
                                    p[I::clipBass], p[I::clipBypass]);
            else if (proc == "limiter")
                chain.lim->process(l, r, l, r, block, p[I::limGain], p[I::limCeiling], p[I::limLookahead],
                                   p[I::limRel], p[I::limAuto] >= 0.5, p[I::limBypass]);
            else
                chain.process(l, r, block, p);
            for (int i = 0; i < block && fr + i < frames; ++i) {
                outL[size_t(fr + i)] = bL[size_t(i)];
                outR[size_t(fr + i)] = bR[size_t(i)];
            }
        }
        golden::quantize(outL);
        golden::quantize(outR);
        const std::string file = c["file"];
        const auto g = golden::load(file);
        std::vector<float> gl(g.size() / 2), gr(g.size() / 2);
        for (size_t i = 0; i < gl.size(); ++i) {
            gl[i] = g[2 * i];
            gr[i] = g[2 * i + 1];
        }
        const auto a = golden::compare(file, outL, gl);
        const auto b = golden::compare(file, outR, gr);
        golden::Result w = a.sampleErrorDb >= b.sampleErrorDb ? a : b;
        w.maxBandDiffDb = std::max(a.maxBandDiffDb, b.maxBandDiffDb);
        w.rmsDb = std::max(a.rmsDb, b.rmsDb);
        w.finite = a.finite && b.finite;
        out.push_back(w);
    }
    return out;
}
