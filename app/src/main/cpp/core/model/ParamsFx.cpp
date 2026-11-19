// Порт Core/Model/Params.swift (ветка swift JAMbo): MOD, FX, мастер, поиск по id.
#include <cmath>
#include <limits>
#include "Params.h"
#include "ParamsUtil.h"
#include "../master/MasterChain.h"

namespace jambo {
using namespace ParamsUtil;
namespace Params {

// MARK: — MOD

const std::vector<std::string> lfoWaves = {"Sine", "Triangle", "Saw", "Square", "Random"};

const std::vector<Div> lfoDivs = {
    {"8 bars", 32}, {"4 bars", 16}, {"2 bars", 8}, {"1 bar", 4}, {"1/2", 2}, {"1/4D", 1.5}, {"1/4", 1},
    {"1/4T", 2.0 / 3}, {"1/8D", 0.75}, {"1/8", 0.5}, {"1/8T", 1.0 / 3}, {"1/16", 0.25}, {"1/16T", 1.0 / 6},
    {"1/32", 0.125},
};
const std::vector<std::string> lfoSyncIds = {"lfo1.sync", "lfo2.sync", "lfo3.sync"};

int lfoDivIndex(double v) {
    const double x = std::log(std::max(lfoSpeedMin, v) / lfoSpeedMin) / std::log(lfoSpeedMax / lfoSpeedMin);
    const int n = int(lfoDivs.size());
    return std::min(n - 1, std::max(0, int(std::round(x * double(n - 1)))));
}

ParamDef lfoSpeedSync(int n) {
    const ParamDef p = lfoColumn(n)[1];
    return ParamDef(p.id, p.label, p.min, p.max, p.def, [](double v) { return std::string(lfoDivs[size_t(lfoDivIndex(v))].name); })
        .withExp();
}

std::string lfoHz(double v) { return v < 1 ? sf("%.2fHz", v) : sf("%.1fHz", v); }

std::vector<ParamDef> lfoColumn(int n) {
    const std::string l = "lfo" + std::to_string(n), L = "LFO" + std::to_string(n);
    static const double speedDef[3] = {0.1, 0.5, 1};
    return {
        stepped(l + ".wave", L + " Wave", lfoWaves),
        ParamDef(l + ".speed", L + " Speed", lfoSpeedMin, lfoSpeedMax, speedDef[n - 1], lfoHz).withExp(),
        ParamDef(l + ".invert", L + " Invert", 0, 1, 0,
                 [](double v) {
                     const long long p = (long long)std::round((1 - 2 * v) * 100);
                     return p == 0 ? std::string("flat") : (p > 0 ? "+" : "") + std::to_string(p) + "%";
                 }),
        // Полярность: 0 — биполярная, 1 — однополярная, между — плавно.
        ParamDef(l + ".polar", L + " Polarity", 0, 1, 0,
                 [](double v) {
                     return v < 0.005 ? std::string("BI") : (v > 0.995 ? std::string("UNI") : "UNI " + ri(v * 100) + "%");
                 }),
    };
}

const std::vector<std::vector<ParamDef>>& modColumns() {
    static const std::vector<std::vector<ParamDef>> c = {
        lfoColumn(1), lfoColumn(2), lfoColumn(3),
        {
            smooth("env.attack", "ENV Attack", 1, 4000, 10, Fmt::ms).withExp(),
            smooth("env.decay", "ENV Decay", 1, 4000, 100, Fmt::ms).withExp(),
            smooth("env.sustain", "ENV Sustain", 0, 100, 40, Fmt::pct),
            smooth("env.release", "ENV Release", 1, 4000, 300, Fmt::ms).withExp(),
        },
    };
    return c;
}

const std::vector<ParamDef>& modGrid() {
    static const std::vector<ParamDef> g = rowsToGrid(modColumns());
    return g;
}

const std::vector<std::string>& modIds() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        for (const auto& col : modColumns()) for (const auto& p : col) v.push_back(p.id);
        return v;
    }();
    return ids;
}

// MARK: — FX

const std::vector<Div> delayDivs = {
    {"1/32", 0.125}, {"1/16T", 1.0 / 6}, {"1/16", 0.25}, {"1/16D", 0.375}, {"1/8T", 1.0 / 3}, {"1/8", 0.5},
    {"1/8D", 0.75}, {"1/4T", 2.0 / 3}, {"1/4", 1}, {"1/4D", 1.5}, {"1/2", 2}, {"1 bar", 4},
};

std::pair<int, double> delayFilterFreq(double v) {
    const double a = std::fabs(v) / 100;
    if (a < 0.02) return {0, 0};
    if (v < 0) return {-1, 20000 * std::pow(500.0 / 20000, a)};
    return {1, 20 * std::pow(1500.0 / 20, a)};
}

static std::vector<std::string> delayDivNames() {
    std::vector<std::string> v;
    for (const auto& d : delayDivs) v.push_back(d.name);
    return v;
}

const std::vector<std::vector<ParamDef>>& fxColumns() {
    static const std::vector<std::vector<ParamDef>> c = {
        {
            smooth("rev.size", "Rev Size", 0, 100, 30, Fmt::pct),
            smooth("rev.decay", "Rev Decay", 0.1, 20, 1.5, [](double v) { return sf("%.1fs", v); }).withExp(),
            smooth("rev.tone", "Rev Tone", -100, 100, 0,
                   [](double v) {
                       const long long r = (long long)std::round(v);
                       return r > 0 ? "+" + std::to_string(r) : std::to_string(r);
                   })
                .withBipolar(),
            smooth("rev.hpf", "Rev Low Cut", 20, 2000, 20, Fmt::hz).withExp(),
        },
        {
            stepped("dly.time", "Dly Time", delayDivNames(), 5),
            smooth("dly.feedback", "Dly Feedback", 0, 100, 40, Fmt::pct),
            // Spread и Filter поменяны местами; порядок в снимках — fxIds.
            smooth("dly.spread", "Dly Spread", 0, 100, 0, Fmt::pct),
            smooth("dly.filter", "Dly Filter", -100, 100, 0,
                   [](double v) {
                       const auto f = delayFilterFreq(v);
                       return f.first == 0 ? std::string("OFF") : std::string(f.first < 0 ? "LP" : "HP") + " " + Fmt::hz(f.second);
                   })
                .withBipolar(),
        },
        {
            // Amount: на дорожке Drive — посыл, здесь — сила искажения.
            smooth("od.drive", "Amount", 0, 100, 26, Fmt::pct),
            stepped("od.type", "Drive Type", {"Soft", "Hard", "Tube", "Fuzz", "Fold", "Crush"}),
            smooth("od.tone", "Drive Tone", 0, 100, 50, Fmt::pct),
            smooth("od.lowcut", "Drive Low Cut", 20, 1000, 20,
                   [](double v) { return v <= driveLowCutOff ? std::string("OFF") : Fmt::hz(v); })
                .withExp(),
        },
        {
            smooth("sc.attack", "Duck Attack", 0.1, 100, 5, Fmt::ms).withExp(),
            smooth("sc.release", "Duck Release", 10, 1000, 150, Fmt::ms).withExp(),
            stepped("sc.source", "Duck Source",
                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"}),
            smooth("sc.curve", "Duck Curve", 0, 100, 50, Fmt::pct),
        },
    };
    return c;
}

const std::vector<ParamDef>& fxGrid() {
    static const std::vector<ParamDef> g = rowsToGrid(fxColumns());
    return g;
}

const std::vector<ParamDef>& hiddenFx() {
    static const std::vector<ParamDef> h = {
        smooth("rev.predelay", "Rev Predelay", 0, 200, 0, Fmt::ms),
        smooth("dly.sync", "Dly Sync", 0, 1, 1, [](double v) { return std::string(v >= 0.5 ? "SYNC" : "FREE"); }),
    };
    return h;
}

const std::vector<std::string> fxIds = {
    "rev.size", "dly.time", "od.drive", "sc.attack", "rev.decay", "dly.feedback", "od.type", "sc.release",
    "rev.tone", "dly.filter", "od.tone", "sc.source", "rev.hpf", "dly.spread", "od.lowcut", "sc.curve",
    "rev.predelay", "dly.sync",
};

double dlyFreeMs(double v) {
    const double x = std::min(dlyFreeTop, std::max(0.0, v)) / dlyFreeTop;
    return dlyFreeMin * std::pow(dlyFreeMax / dlyFreeMin, x);
}

double dlyFreeValue(double ms) {
    const double m = std::min(dlyFreeMax, std::max(dlyFreeMin, ms));
    return dlyFreeTop * std::log(m / dlyFreeMin) / std::log(dlyFreeMax / dlyFreeMin);
}

const ParamDef& dlyTimeFree() {
    static const ParamDef p = smooth("dly.time", "Dly Time", 0, dlyFreeTop, 5, [](double v) {
        const double ms = dlyFreeMs(v);
        return ms >= 1000 ? sf("%.2fs", ms / 1000) : ri(ms) + "ms";
    });
    return p;
}

int nearestDelayDiv(double ms, double bpm) {
    int best = 0;
    double bd = std::numeric_limits<double>::max();
    for (size_t k = 0; k < delayDivs.size(); ++k) {
        const double t = delayDivs[k].beats * 60000 / std::max(1.0, bpm);
        const double e = std::fabs(std::log(t / std::max(1.0, ms)));
        if (e < bd) {
            bd = e;
            best = int(k);
        }
    }
    return best;
}

ParamMap fxDefaults() {
    ParamMap d;
    for (const auto& p : fxGrid()) d[p.id] = p.def;
    for (const auto& p : hiddenFx()) d[p.id] = p.def;
    d["rev.hpf"] = revHpfNew;
    d["od.lowcut"] = driveLowCutNew;
    return d;
}

// MARK: — Мастер

std::string masterDb(double v) { return (v > 0.04 ? "+" : "") + sf("%.1f\ndB", v); }

std::string masterMs(double v) {
    if (v >= MasterChain::autoValue) return "AUTO";
    if (v >= 1000) return sf("%.1f s", v / 1000);
    if (v < 10) return sf("%.1f ms", v);
    return ri(v) + " ms";
}

const std::vector<std::string> satTypes = {"TAPE", "TUBE", "EDGE"};

const std::vector<std::vector<ParamDef>>& masterColumns() {
    static const std::vector<std::vector<ParamDef>> c = {
        {
            master("sat.drive", "Drive", 0, 24, 0, masterDb).withStep(0.1),
            ParamDef("sat.type", "Type", 0, 2, 0,
                     [](double v) { return satTypes[size_t(std::min(2.0, std::max(0.0, std::round(v))))]; })
                .withKind(KnobKind::stepped)
                .withOptions(satTypes)
                .withNoLock(),
            master("sat.tone", "Tone", -100, 100, 0, [](double v) { return masterDb(v / 100 * 6); }),
            master("sat.mix", "Mix", 0, 100, 100, Fmt::pct),
        },
        {
            master("cmp.threshold", "Threshold", -40, 0, -10, masterDb).withStep(0.1),
            master("cmp.attack", "Attack", 0.1, 100, MasterChain::newAttack, masterMs).withExp(),
            master("cmp.release", "Release", 20, 2000, 400, masterMs).withExp().withAutoTop(0.08),
            master("cmp.makeup", "Makeup", 0, 18, 0, masterDb).withStep(0.1),
        },
        {
            master("clip.drive", "Input", 0, 12, 0, masterDb).withStep(0.1),
            master("clip.ceiling", "Ceiling", -12, 0, -1, masterDb).withStep(0.1),
            master("clip.softness", "Softness", 0, 100, 30, Fmt::pct),
            master("clip.bass", "Bass", 20, 300, 0, [](double v) { return v <= 0 ? std::string("OFF") : ri(v) + " Hz"; })
                .withExp()
                .withOffBottom(0.05),
        },
        {
            master("lim.input", "Input", 0, 24, 0, masterDb).withStep(0.1),
            master("lim.ceiling", "Ceiling", -24, 0, -1, masterDb).withStep(0.1),
            master("lim.lookahead", "Lookahead", 0.5, 6, 3, [](double v) { return sf("%.1f ms", v); }),
            master("lim.release", "Release", 10, 1000, 150, masterMs).withExp().withAutoTop(0.08),
        },
    };
    return c;
}

std::string masterHz(double v) {
    if (v <= 20.5) return "OFF";
    if (v >= 1000) return sf("%.2f kHz", v / 1000);
    return ri(v) + " Hz";
}

const std::vector<ParamDef>& masterExtra() {
    static const std::vector<ParamDef> e = {
        master("sat.focus", "Focus", 20, 2000, 20, masterHz).withExp(),
        master("cmp.ratio", "Ratio", 1, 20, 2,
               [](double v) { return v < 9.95 ? sf("%.1f:1", v) : ri(v) + ":1"; })
            .withExp()
            .withStep(0.01),
        master("cmp.knee", "Knee", 0, 24, 6, [](double v) { return sf("%.1f dB", v); }).withStep(0.1),
        master("cmp.mix", "Mix", 0, 100, 100, Fmt::pct),
        master("cmp.schpf", "SC HPF", 20, 500, 90, masterHz).withExp(),
        // GUARD лимитера: предел среднего GR 2…8 dB, первые 8 % хода — OFF.
        master("lim.guard", "Guard", 2, 8, 0, [](double v) { return v <= 0 ? std::string("OFF") : sf("%.1f dB", v); })
            .withStep(0.5)
            .withOffBottom(0.08),
    };
    return e;
}

const std::vector<ParamDef>& masterGrid() {
    static const std::vector<ParamDef> g = rowsToGrid(masterColumns());
    return g;
}

const ParamDef* byId(const std::string& id) {
    static const std::map<std::string, const ParamDef*> d = [] {
        std::map<std::string, const ParamDef*> m;
        for (const auto* list : {&trackGrid(), &synthColumn(), &modGrid(), &fxGrid(), &hiddenFx(), &masterGrid(),
                                 &masterExtra()})
            for (const auto& p : *list) if (!p.id.empty()) m[p.id] = &p;
        return m;
    }();
    const auto it = d.find(id);
    return it != d.end() ? it->second : nullptr;
}

ParamMap defaults() {
    ParamMap d;
    for (const auto* list : {&trackGrid(), &synthColumn(), &modGrid()})
        for (const auto& p : *list) if (!p.id.empty()) d[p.id] = p.def;
    d[swing().id] = swing().def;
    return d;
}

}  // namespace Params
}  // namespace jambo
