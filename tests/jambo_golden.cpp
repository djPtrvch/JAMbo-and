// Сверка своих движков (KICK, ANALOG, FM, WTABLE) с эталонами j*.i16 (порт Golden/JamboSelfTest.swift JAMbo):
// блоки по 128 кадров (дрейф ANALOG — в начале блока, время дорожки — в конце), внутри — куски по 24 и до кадра
// следующей ноты; тишина дольше 4800 кадров — голос молчит. Ручки случая переводятся migrateKnobs; случаи, где звук
// после S12 другой (суб ANALOG, клик KICK не на 50 %), не сравниваются.
#include "golden.h"
#include "synth/JamboVoice.h"
#include "model/SynthMigration.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <climits>
#include <deque>
#include <fstream>
#include <map>
#include <memory>

using nlohmann::json;
using namespace jambo;

namespace {

bool comparable(const json& c) {
    const int e = c["engine"];
    const double p3 = c["p3"];
    if (e == J::analog) return p3 == 0;
    if (e == J::drum) return std::fabs(p3 - 0.5) < 1e-9;
    return true;
}

std::vector<float> render(const json& c, const json& all, const WtRef& ref) {
    const double sr = 48000;
    const int total = all["frames"], blockN = all["block"], chunk = all["chunk"];
    JamboVoice voice(JamboTables::shared(), sr);
    JamboDrift drift;
    std::vector<float> result(total, 0);
    std::vector<float> buf(chunk);
    std::deque<int> pending{0};
    if (c["retrig"].get<int>() > 0) pending.push_back(c["retrig"]);
    int end = INT_MAX, start0 = 0, quiet = 0;
    const int engine = c["engine"];
    const double note = c["note"], decay = c["decay"];
    const MigratedKnobs k = migrateKnobs(engine, c["p1"].get<double>() * 100, c["p2"].get<double>() * 100,
                                         c["p3"].get<double>() * 100);
    for (int blockStart = 0; blockStart < total; blockStart += blockN) {
        const int frames = std::min(blockN, total - blockStart);
        drift.begin(blockN, sr);
        int pos = 0;
        while (pos < frames) {
            const int now = blockStart + pos;
            while (!pending.empty() && pending.front() <= now) {
                const int f = pending.front();
                pending.pop_front();
                end = INT_MAX;
                start0 = f;
                quiet = 0;
                voice.trigger(engine, note, k.h / 100, k.t / 100, k.m / 100, k.p4 / 100, decay,
                              double(now), drift.tau + double(now - blockStart) * drift.ratio);
            }
            int n = std::min(chunk, frames - pos);
            if (!pending.empty()) n = std::min(n, pending.front() - now);
            if (now >= end) {
                voice.last = 0;
            } else {
                voice.render(buf.data(), n, drift.ratio, ref);
                float peak = 0;
                for (int i = 0; i < n; ++i) {
                    result[now + i] = buf[i];
                    peak = std::max(peak, std::fabs(buf[i]));
                }
                quiet = peak < 1e-5f ? quiet + n : 0;
                if (quiet > 4800 && now - start0 > 4800) end = now;
            }
            pos += n;
        }
        drift.end(blockN);
    }
    golden::quantize(result);
    return result;
}

}  // namespace

std::vector<golden::Result> runJambo() {
    std::ifstream f(golden::dir() + "/jambo_cases.json");
    const json all = json::parse(f);
    std::map<std::string, std::unique_ptr<WtTable>> tables;
    std::vector<golden::Result> out;
    for (const auto& c : all["cases"]) {
        if (!comparable(c)) continue;
        WtRef ref;
        if (c["engine"].get<int>() == J::wtable) {
            const std::string name = c.value("table", std::string("BASIC"));
            auto& t = tables[name];
            if (!t) t = WtTable::builtin(name);
            ref = t->ref();
        }
        const std::string file = c["file"];
        out.push_back(golden::compare(file, render(c, all, ref), golden::load(file)));
    }
    return out;
}
