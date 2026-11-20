// Иконки движков (порт EngineIconTests.swift): против эталона прототипа (ios/tools/icons/engine_previews_golden.json),
// сетка значений ручек — без NaN и в поле 64×64, каждая ручка меняет картинку, кадр таблицы BASIC.
#include "icons/EngineIcons.h"
#include "synth/Wavetable.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>

using namespace jambo;
using nlohmann::json;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        if (fails < 20) std::printf("%-70s FAIL\n", what.c_str());
        ++fails;
    }
}

const int engines[] = {16, 17, 18, 19, 20, 21, 22, 23, 24};

std::vector<std::pair<double, double>> points(const std::vector<IconCmd>& cmds) {
    std::vector<std::pair<double, double>> p;
    for (const auto& c : cmds)
        if (c.kind == IconCmd::move || c.kind == IconCmd::line) p.emplace_back(c.x, c.y);
    return p;
}

EngineIcons::Elements visible(const EngineIcons::Elements& els) {
    EngineIcons::Elements out;
    for (const auto& e : els)
        if (!e.rects.empty() || !e.cmds.empty() || e.text) out.push_back(e);
    return out;
}

/// Соседние прямоугольники с тем же x, касающиеся или перекрытые по y, — один (mergeRuns в golden.mjs).
std::vector<IconRect> mergeRuns(const std::vector<IconRect>& rects) {
    std::vector<IconRect> out;
    for (const auto& r : rects) {
        if (!out.empty() && std::abs(out.back().x - r.x) < 1e-3 && r.y <= out.back().y + out.back().h + 1e-3) {
            IconRect& p = out.back();
            p.h = std::max(p.y + p.h, r.y + r.h) - p.y;
        } else {
            out.push_back(r);
        }
    }
    return out;
}

void golden() {
    std::ifstream f(ICONS_GOLDEN);
    check(f.good(), "icons: эталон есть (scripts/fetch-test-deps.sh)");
    if (!f.good()) return;
    const json all = json::parse(f);
    std::set<int> seen;
    for (const auto& g : all) {
        const int e = g["engine"];
        seen.insert(e);
        const auto& jv = g["v"];
        IconValues v;
        v.h = jv["h"];
        v.t = jv["t"];
        v.m = jv["m"];
        v.q = jv["q"];
        v.d = jv["d"];
        std::optional<double> hx;
        if (!g["hx"].is_null()) hx = g["hx"].get<double>();
        auto els = visible(EngineIcons::elements(e, v, hx));
        if (e == 21 && els.size() == 2) els[1].rects = mergeRuns(els[1].rects);
        std::ostringstream nm;
        nm << "icons: engine " << e << " h" << v.h << " t" << v.t << " m" << v.m << " q" << v.q << " d" << v.d
           << (hx ? " hx" : "");
        const auto& ops = g["ops"];
        check(els.size() == ops.size(), nm.str() + " — число элементов");
        for (size_t k = 0; k < std::min(els.size(), ops.size()); ++k) {
            const auto& a = els[k];
            const auto& b = ops[k];
            const std::string at = nm.str() + " op " + std::to_string(k);
            check((a.ink == IconInk::secondary ? "s" : "p") == b["ink"].get<std::string>(), at + " цвет");
            check(std::abs(a.opacity.value_or(1) - b["a"].get<double>()) <= 0.0002, at + " прозрачность");
            if (b.contains("text") && !b["text"].is_null()) {
                const auto& tx = b["text"];
                check(a.text && *a.text == tx[0].get<std::string>() && a.textX == tx[1].get<double>() &&
                          a.textY == tx[2].get<double>(),
                      at + " текст");
                continue;
            }
            const auto& br = b["r"];
            check(a.rects.size() == br.size(), at + " число прямоугольников");
            for (size_t i = 0; i < std::min(a.rects.size(), br.size()); ++i) {
                const auto& ra = a.rects[i];
                const double d = std::max({std::abs(ra.x - br[i][0].get<double>()), std::abs(ra.y - br[i][1].get<double>()),
                                           std::abs(ra.w - br[i][2].get<double>()), std::abs(ra.h - br[i][3].get<double>())});
                if (d > 0.002) {
                    check(false, at + " прямоугольник " + std::to_string(i));
                    break;
                }
            }
            const auto pa = points(a.cmds);
            const auto& bp = b["poly"];
            check(pa.size() == bp.size(), at + " число точек");
            for (size_t i = 0; i < std::min(pa.size(), bp.size()); ++i) {
                if (std::abs(pa[i].first - bp[i][0].get<double>()) > 0.002 ||
                    std::abs(pa[i].second - bp[i][1].get<double>()) > 0.002) {
                    check(false, at + " точка " + std::to_string(i));
                    break;
                }
            }
        }
    }
    check(seen == std::set<int>(std::begin(engines), std::end(engines)), "icons: эталон — движки 16–24");
}

bool inside(double x, double y) { return std::isfinite(x) && std::isfinite(y) && x >= 0 && x <= 64 && y >= 0 && y <= 64; }

bool bounded(const EngineIcons::Elements& els) {
    for (const auto& el : els) {
        if (el.opacity && !(std::isfinite(*el.opacity) && *el.opacity >= 0 && *el.opacity <= 1)) return false;
        for (const auto& r : el.rects)
            if (!(inside(r.x, r.y) && inside(r.x + r.w, r.y + r.h) && r.w >= 0 && r.h >= 0)) return false;
        for (const auto& p : points(el.cmds))
            if (!inside(p.first, p.second)) return false;
        if (el.text && !inside(el.textX, el.textY)) return false;
    }
    return true;
}

void grid() {
    const double knob[4] = {0, 1.0 / 3, 2.0 / 3, 1};
    for (int e : engines) {
        bool ok = true;
        for (double h : knob) for (double t : knob) for (double m : knob) for (double q : knob)
            for (double d : {0.0, 0.5, 1.0}) {
                IconValues v;
                v.h = h, v.t = t, v.m = m, v.q = q, v.d = d;
                const auto els = EngineIcons::elements(e, v);
                if (visible(els).empty() || !bounded(els)) ok = false;
                if (d == 0.5 && (h == 0 || h == 1) && !bounded(EngineIcons::elements(e, v, 30.0))) ok = false;
            }
        check(ok, "icons: сетка ручек — в поле, без NaN, engine " + std::to_string(e));
    }
}

std::string sig(const EngineIcons::Elements& els) {
    std::ostringstream o;
    for (const auto& e : els) {
        for (const auto& r : e.rects) o << r.x << ',' << r.y << ',' << r.w << ',' << r.h << ';';
        for (const auto& c : e.cmds) o << c.x << ',' << c.y << ';';
        o << e.opacity.value_or(-1) << '|';
    }
    return o.str();
}

void eachKnob() {
    const std::set<int> decay = {16, 18, 20, 21, 22, 23};
    for (int e : engines) {
        const IconValues base;
        const std::string s0 = sig(EngineIcons::elements(e, base));
        double IconValues::*knobs[5] = {&IconValues::h, &IconValues::t, &IconValues::m, &IconValues::q, &IconValues::d};
        const int n = decay.count(e) ? 5 : 4;
        for (int k = 0; k < n; ++k) {
            IconValues v = base;
            v.*knobs[k] = 0.9;
            check(sig(EngineIcons::elements(e, v)) != s0, "icons: ручка меняет иконку, engine " + std::to_string(e) +
                                                              " knob " + std::to_string(k));
        }
    }
}

void table() {
    const auto tb = WtTable::builtin("BASIC");
    const auto f = EngineIcons::tableFrame(*tb);
    check(std::abs(f(0, 0.25) - 1) <= 0.05 && std::abs(f(0, 1.25) - 1) <= 0.05, "icons: кадр BASIC — синус");
    bool ok = true;
    for (double pos : {0.0, 0.5, 1.0})
        for (double warp : {0.0, 0.7}) {
            IconValues v;
            v.h = pos, v.t = warp, v.m = 1, v.q = 1;
            if (!bounded(EngineIcons::wtableIcon(v, f))) ok = false;
        }
    check(ok, "icons: WTABLE по таблице — в поле");
}
}  // namespace

int runIconTests() {
    golden();
    grid();
    eachKnob();
    table();
    std::printf("icons: %d / %d passed\n", checks - fails, checks);
    return fails;
}
