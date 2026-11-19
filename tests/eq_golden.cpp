// Сверка EQ с эталонами eq_*.i16 (порт Golden/EqSelfTest.swift JAMbo): EQ начинается пустым, первые полосы вводятся
// кроссфейдом; события — новый EQ в кадре блока; блоки по 128. Вход — как MasterSelfTest.input.
#include "golden.h"
#include "eq/EqSnapshot.h"
#include "eq/EqUnit.h"
#include "fx/FxRandom.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>

using nlohmann::json;
using namespace jambo;

namespace {

void input(const std::string& kind, int frames, std::vector<float>& l, std::vector<float>& r) {
    l.assign(size_t(frames), 0.f);
    r.assign(size_t(frames), 0.f);
    const double sr = 48000;
    if (kind == "sine") {
        for (int i = 0; i < std::min(24000, frames); ++i) {
            const float v = float(0.25 * std::sin(2 * M_PI * 440 * double(i) / sr));
            l[size_t(i)] = v;
            r[size_t(i)] = v;
        }
    } else if (kind == "stereo") {
        FxRandom rnd(11);
        for (int i = 0; i < std::min(1440, frames); ++i) {
            l[size_t(i)] = float((rnd.next() * 2 - 1) * 0.5);
            r[size_t(i)] = float(0.3 * std::sin(2 * M_PI * 660 * double(i) / sr));
        }
    } else if (kind == "drums") {
        FxRandom rnd(5);
        for (int i = 0; i < frames; ++i) {
            const double t = double(i % 12000) / sr;
            const double th = double((i + 6000) % 12000) / sr;
            const double ph = 55 * t + 40 * 0.03 * (1 - std::exp(-t / 0.03));
            const double kick = 0.9 * std::exp(-t / 0.12) * std::sin(2 * M_PI * ph);
            const double hat = 0.4 * std::exp(-th / 0.015) * (rnd.next() * 2 - 1);
            l[size_t(i)] = float(kick + hat);
            r[size_t(i)] = float(kick + hat + 0.2 * std::sin(2 * M_PI * 220 * double(i) / sr));
        }
    } else if (kind == "noise") {
        FxRandom a(3), b(4);
        for (int i = 0; i < frames; ++i) {
            l[size_t(i)] = float((a.next() * 2 - 1) * 0.3);
            r[size_t(i)] = float((b.next() * 2 - 1) * 0.3);
        }
    } else if (kind == "loud") {
        for (int i = 0; i < frames; ++i) {
            const double v = i < 24000 ? std::sin(2 * M_PI * 1000 * double(i) / sr)
                                       : 0.708 * std::sin(2 * M_PI * 5000 * double(i) / sr);
            l[size_t(i)] = float(v);
            r[size_t(i)] = float(0.9 * v);
        }
    }
}

Eq parseEq(const json& o) {
    Eq eq;
    eq.on = o.is_object() && o.contains("on") ? o["on"].get<bool>() : true;
    if (!o.is_object() || !o.contains("bands")) return eq;
    int k = 0;
    for (const auto& b : o["bands"]) {
        if (k >= EqSpec::bands) break;
        if (b.is_object()) {
            auto num = [&](const char* key) { return b.contains(key) ? b[key].get<double>() : 0.0; };
            EqBand band(b.contains("on") ? b["on"].get<bool>() : true, int(num("type")), num("freq"), num("gain"),
                        num("q"), int(num("slope")), int(num("ms")));
            if (b.contains("dyn")) band.dyn = b["dyn"].get<int>();
            if (b.contains("thr")) band.thr = b["thr"].get<double>();
            if (b.contains("range")) band.range = b["range"].get<double>();
            eq.bands[size_t(k)] = band;
        }
        ++k;
    }
    return eq;
}

}  // namespace

std::vector<golden::Result> runEq() {
    std::ifstream f(golden::dir() + "/eq_cases.json");
    const json all = json::parse(f);
    std::vector<golden::Result> out;
    const int n = 128;
    for (const auto& c : all["cases"]) {
        const int frames = c["frames"];
        std::vector<std::pair<int, Eq>> eqs{{0, parseEq(c["eq"])}};
        if (c.contains("events"))
            for (const auto& e : c["events"]) eqs.push_back({e["frame"].get<int>(), parseEq(e["eq"])});
        std::vector<float> iL, iR, sL, sR;
        input(c["input"], frames, iL, iR);
        const bool hasSrc = c.contains("source") && c["source"].is_string();
        if (hasSrc) input(c["source"], frames, sL, sR);
        std::vector<float> outL(static_cast<size_t>(frames)), outR(static_cast<size_t>(frames)), bL(n), bR(n), tL(n), tR(n);
        std::vector<double> p(EqSpec::stride);
        const Eq empty;
        EqSpec::fill(p.data(), &empty);
        EqUnit unit(48000);
        if (hasSrc) {
            unit.trackL = tL.data();
            unit.trackR = tR.data();
            unit.trackStride = n;
        }
        for (int fr = 0; fr < frames; fr += n) {
            for (const auto& e : eqs)
                if (e.first == fr) EqSpec::fill(p.data(), &e.second);
            for (int i = 0; i < n; ++i) {
                const size_t j = size_t(fr + i);
                bL[size_t(i)] = fr + i < frames ? iL[j] : 0;
                bR[size_t(i)] = fr + i < frames ? iR[j] : 0;
                tL[size_t(i)] = j < sL.size() ? sL[j] : 0;
                tR[size_t(i)] = j < sR.size() ? sR[j] : 0;
            }
            unit.process(bL.data(), bR.data(), n, p.data());
            for (int i = 0; i < n && fr + i < frames; ++i) {
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
