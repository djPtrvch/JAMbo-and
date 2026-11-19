// Headphone Monitor: порт MonitorTests (без AudioCore и PhonesProfile), RoomTests, RoomHeadTests (без пакетов голов)
// ветки swift JAMbo. Ресурсы комнат — ios/JAMbo.swiftpm/Resources (scripts/fetch-test-deps.sh).
#include "eq/EqCoeffs.h"
#include "golden.h"
#include "monitor/MonitorChain.h"
#include "monitor/MonitorDesign.h"
#include "monitor/RoomConv.h"
#include "monitor/RoomIR.h"
#include "sampler/PvFFT.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace jambo;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}
bool near(double a, double b, double e) { return std::fabs(a - b) <= e; }
constexpr int B = 256;

std::string res() { return golden::dir() + "/../../JAMbo.swiftpm/Resources/"; }
std::string readText(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
std::vector<uint8_t> readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

Eq sony() {
    Eq e;
    const int bell = EqSpec::bell;
    const EqBand b[10] = {EqBand(true, bell, 56, 1.2, 1.19),       EqBand(true, EqSpec::lowShelf, 105, -4.2, 0.71),
                          EqBand(true, bell, 143, -5.2, 1.1),      EqBand(true, bell, 407, 1.7, 3.14),
                          EqBand(true, bell, 576, -1.2, 3.55),     EqBand(true, bell, 1007, 1.0, 3.41),
                          EqBand(true, bell, 2289, 6.1, 1.57),     EqBand(true, bell, 5144, -3.2, 6.0),
                          EqBand(true, bell, 6715, 3.0, 5.99),     EqBand(true, EqSpec::highShelf, 10000, -1.0, 0.71)};
    for (int k = 0; k < 10; ++k) e.bands[size_t(k)] = b[k];
    return e;
}
Eq curve(const std::vector<double>& hz, const std::vector<double>& g) {
    Eq e;
    e.bands.clear();
    for (size_t i = 0; i < hz.size(); ++i) e.bands.push_back(EqBand(true, EqSpec::bell, hz[i], g[i], 1));
    return e;
}
TranslationPreset phone() {
    TranslationPreset p;
    p.name = "Phone";
    p.mono = true;
    p.filters = {{"HP", 300, {}, {}, {}}, {"LP", 12000, {}, {}, {}}, {"PK", 2500, 3.0, 1.0, {}}};
    return p;
}

using Sig = std::function<float(int, int)>;
struct Out {
    std::vector<float> l, r, inL, inR;
};
Out run(MonitorChain& c, int blocks, const Sig& f, bool raw = false) {
    Out o;
    std::vector<float> l(B), r(B);
    int t = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < B; ++i) {
            l[size_t(i)] = f(t + i, 0);
            r[size_t(i)] = f(t + i, 1);
        }
        o.inL.insert(o.inL.end(), l.begin(), l.end());
        o.inR.insert(o.inR.end(), r.begin(), r.end());
        c.process(l.data(), r.data(), B, raw);
        o.l.insert(o.l.end(), l.begin(), l.end());
        o.r.insert(o.r.end(), r.begin(), r.end());
        t += B;
    }
    return o;
}
Sig noise(uint64_t seed) {
    return [seed](int t, int ch) {
        uint64_t x = seed + uint64_t(t * 2 + ch) * 0x9E3779B97F4A7C15ull;
        x ^= x >> 31;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 29;
        return float(double(x % 100000) / 100000 - 0.5) * 0.8f;
    };
}
double gain(MonitorChain& c, double hz, double sr, double amp = 0.1) {
    const int blocks = int(sr * 1.5) / B;
    const Out o = run(c, blocks, [&](int t, int) { return float(amp * std::sin(2 * M_PI * hz * double(t) / sr)); });
    const int n = int(sr * 0.5);
    double s = 0, k = 0;
    for (int i = int(o.l.size()) - n; i < int(o.l.size()); ++i) {
        const double w = 2 * M_PI * hz * double(i) / sr;
        s += double(o.l[size_t(i)]) * std::sin(w);
        k += double(o.l[size_t(i)]) * std::cos(w);
    }
    return 20 * std::log10(2 * std::sqrt(s * s + k * k) / double(n) / amp);
}
std::vector<double> make(const Eq* eq, const MonitorSettings& m, const Eq* correct = nullptr,
                         const TranslationPreset* preset = nullptr, double sr = 48000) {
    return MonitorDesign::make(eq, m, correct, preset, nullptr, 0, sr);
}

void neutralAndBypass() {
    MonitorSettings m;
    m.strength = 0;
    const Eq s = sony();
    MonitorChain c(48000);
    const auto p = make(&s, m);
    c.publish(p);
    const Out o = run(c, 50, noise(1));
    check(p[MonitorSpec::compAt] == 1 && o.l == o.inL && o.r == o.inR, "monitor: Strength 0 — бит в бит");
    MonitorSettings full;
    full.sub = true;
    const Eq cc = curve({100, 400, 1500, 8000}, {2, -2, 1, 3});
    const TranslationPreset ph = phone();
    MonitorChain b(48000);
    b.publish(make(&s, full, &cc, &ph));
    run(b, 20, noise(2));
    Eq off = sony();
    off.on = false;
    b.publish(make(&off, full, &cc, &ph));
    run(b, 10, noise(3));
    const Out o2 = run(b, 20, noise(4));
    check(o2.l == o2.inL && o2.r == o2.inR, "monitor: Bypass — бит в бит");
}

void correctionResponse() {
    MonitorSettings m;
    const Eq e = sony();
    MonitorChain c(48000);
    const auto p = make(&e, m);
    c.publish(p);
    const double comp = 20 * std::log10(p[MonitorSpec::compAt]);
    bool ok = comp != 0;
    for (double hz : {40.0, 105.0, 143.0, 1000.0, 2289.0, 5144.0, 6715.0, 12000.0}) {
        double want = comp;
        for (const auto& b : e.bands)
            if (b) want += EqCoeffs::response(*b, hz);
        ok = ok && near(gain(c, hz, 48000), want, 0.1);
    }
    check(ok, "monitor: АЧХ коррекции");
    m.strength = 0.5;
    const auto p2 = make(&e, m);
    Eq half = e;
    for (auto& b : half.bands)
        if (b) b->gain *= 0.5;
    double want = 0;
    for (const auto& b : half.bands)
        if (b) want += EqCoeffs::response(*b, 2289);
    check(near(MonitorDesign::response(p2, 2289, 48000), want, 1e-6), "monitor: Strength 50 %");
    Eq one;
    one.bands[0] = EqBand(true, EqSpec::bell, 1000, 18, 1);
    check(near(MonitorDesign::response(make(&one, MonitorSettings()), 1000, 48000), 12, 1e-6), "monitor: предел ±12 dB");
    MonitorSettings z;
    z.strength = 0;
    std::vector<double> hz, g;
    for (int i = 0; i < 14; ++i) {
        hz.push_back(50 * std::pow(1.6, double(i)));
        g.push_back(1);
    }
    const Eq cc = curve(hz, g);
    double w = 0;
    for (const auto& b : cc.bands)
        if (b) w += EqCoeffs::response(*b, 1000);
    check(cc.bands.size() == 14 && near(MonitorDesign::response(make(&e, z, &cc), 1000, 48000), w, 1e-6) && w > 1.5,
          "monitor: CORRECT CURVE");
}

void limiter() {
    const Eq e = sony();
    MonitorChain c(48000);
    c.publish(make(&e, MonitorSettings()));
    const Out o = run(c, 40, [](int t, int) { return float(2 * std::sin(2 * M_PI * 200 * double(t) / 48000)); });
    float pk = 0;
    for (size_t i = o.l.size() - 4800; i < o.l.size(); ++i) pk = std::max({pk, std::fabs(o.l[i]), std::fabs(o.r[i])});
    const double ceil = std::pow(10.0, -0.5 / 20);
    check(pk <= ceil + 1e-6 && pk > ceil - 0.01 && c.limitGain.load() < 1, "monitor: лимитер −0,5 dBFS");
    MonitorChain d(48000);
    d.publish(make(&e, MonitorSettings()));
    const Out ob = run(d, 600, [](int t, int) { return float(1.6 * std::sin(2 * M_PI * 50 * double(t) / 48000)); });
    const size_t t0 = ob.l.size() - 48000;
    double ss = 0, cc = 0;
    for (size_t i = t0; i < ob.l.size(); ++i) {
        const double w = 2 * M_PI * 50 * double(i) / 48000;
        ss += ob.l[i] * std::sin(w);
        cc += ob.l[i] * std::cos(w);
    }
    const double a = 2 * ss / 48000, b = 2 * cc / 48000;
    double err = 0, sig = 0;
    float p = 0;
    for (size_t i = t0; i < ob.l.size(); ++i) {
        const double w = 2 * M_PI * 50 * double(i) / 48000;
        const double fit = a * std::sin(w) + b * std::cos(w);
        err += std::pow(ob.l[i] - fit, 2);
        sig += fit * fit;
        p = std::max(p, std::fabs(ob.l[i]));
    }
    check(10 * std::log10(err / sig) < -60 && p <= ceil + 1e-6, "monitor: бас выше потолка — без искажений");
}

void sampleRates() {
    const Eq e = sony();
    bool ok = true;
    for (double sr : {44100.0, 96000.0}) {
        MonitorChain c(sr);
        const auto p = make(&e, MonitorSettings(), nullptr, nullptr, sr);
        c.publish(p);
        const double comp = 20 * std::log10(p[MonitorSpec::compAt]);
        for (double hz : {143.0, 2289.0, 6715.0})
            ok = ok && near(gain(c, hz, sr), MonitorDesign::response(p, hz, sr) + comp, 0.1);
    }
    check(ok, "monitor: 44,1 и 96 кГц");
}

void brokenAndSmooth() {
    check(TranslationPreset::list("{bad").empty() && TranslationPreset::list("").empty(), "monitor: битый JSON");
    check(int(make(nullptr, MonitorSettings()).size()) == MonitorSpec::size, "monitor: пустые параметры");
    const auto list = TranslationPreset::list(readText(res() + "TranslationPresets.json"));
    check(list.size() == 3 && list[1].room == std::string("StudioCR7"), "monitor: пресеты Translation");
    const Eq e = sony();
    MonitorChain c(48000);
    c.publish(make(&e, MonitorSettings()));
    run(c, 5, noise(6));
    const Eq empty;
    c.publish(make(&empty, MonitorSettings()));
    c.publish({1, 2, 3});
    const Out o = run(c, 5, noise(7));
    check(std::all_of(o.l.begin(), o.l.end(), [](float v) { return std::isfinite(v); }), "monitor: без NaN");
    const double hz = 1500;
    const double maxStep = 0.3 * 2 * M_PI * hz / 48000;
    MonitorChain s(48000);
    MonitorSettings m;
    Eq ee = sony();
    std::vector<float> out;
    int t0 = 0;
    for (int st = 0; st < 6; ++st) {
        if (st == 1) m.mono = true;
        if (st == 2) m.strength = 0;
        if (st == 3) m.strength = 1;
        if (st == 4) ee.on = false;
        if (st == 5) ee.on = true;
        s.publish(make(&ee, m));
        const Out oo = run(s, 20, [&](int t, int) { return float(0.3 * std::sin(2 * M_PI * hz * double(t + t0) / 48000)); });
        out.insert(out.end(), oo.l.begin(), oo.l.end());
        t0 += 20 * B;
    }
    double worst = 0;
    for (size_t i = 1; i < out.size(); ++i) worst = std::max(worst, double(std::fabs(out[i] - out[i - 1])));
    check(worst < maxStep * 2.2, "monitor: смена параметров без щелчков");
}

// Комнаты.
std::vector<std::shared_ptr<RoomInfo>> rooms() { return RoomInfo::list(readText(res() + "rooms.json")); }
std::shared_ptr<RoomIR> ir(int k) {
    const auto r = rooms()[size_t(k)];
    return RoomIR::fromWav(r->name, readBytes(res() + r->file), r->gainDb, r->direct);
}
std::vector<float> rnoise(int n, uint64_t seed) {
    uint64_t x = seed;
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& s : v) {
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        s = float(double(x >> 33) / double(1ull << 31) - 0.5) * 0.5f;
    }
    return v;
}
struct Pair {
    std::vector<float> l, r;
};
Pair runRoom(RoomStage& st, const std::vector<float>& l, const std::vector<float>& r,
             const std::function<void(int)>& before = nullptr) {
    Pair o;
    std::vector<double> bl(B), br(B);
    for (size_t at = 0, b = 0; at + B <= l.size(); at += B, ++b) {
        if (before) before(int(b));
        for (int i = 0; i < B; ++i) {
            bl[size_t(i)] = l[at + size_t(i)];
            br[size_t(i)] = r[at + size_t(i)];
        }
        st.process(bl.data(), br.data(), B);
        for (int i = 0; i < B; ++i) {
            o.l.push_back(float(bl[size_t(i)]));
            o.r.push_back(float(br[size_t(i)]));
        }
    }
    return o;
}

void roomsAndConv() {
    const auto list = rooms();
    check(list.size() == 2 && list[0]->name == "StudioCR7" && list[1]->name == "Hall" && list[0]->space == "StudioCR7" &&
              list[0]->headName == "FABIAN",
          "room: rooms.json");
    bool ok = true;
    for (int k = 0; k < 2; ++k) {
        const auto x = ir(k);
        ok = ok && x && x->frames > 1000 && x->frames <= RoomIR::maxFrames && x->direct == 128;
    }
    const std::vector<uint8_t> junk = {'j', 'u', 'n', 'k'};
    check(ok && !RoomIR::parseWav(junk) && RoomInfo::list("{").empty(), "room: WAV импульсов");
    const auto h = ir(1);
    for (float amb : {1.f, 0.f}) {
        RoomStage st(48000);
        st.ambience.store(amb);
        st.set(std::make_unique<RoomConv>(*h));
        const int n = B * 60;
        const auto l = rnoise(n, 1), r = rnoise(n, 2);
        const Pair o = runRoom(st, l, r);
        const int fr = h->frames;
        double err = 0, sig = 0;
        for (int t = 4000; t < n - B; ++t) {
            double el = 0, er = 0;
            for (int k = 0; k < fr && t - 128 - k >= 0; ++k) {
                const double g = k < h->direct ? 1 : double(amb);
                const double xl = l[size_t(t - 128 - k)], xr = r[size_t(t - 128 - k)];
                el += g * (double(h->h[size_t(k)]) * xl + double(h->h[size_t(2 * fr + k)]) * xr);
                er += g * (double(h->h[size_t(fr + k)]) * xl + double(h->h[size_t(3 * fr + k)]) * xr);
            }
            err += std::pow(o.l[size_t(t)] - el, 2) + std::pow(o.r[size_t(t)] - er, 2);
            sig += el * el + er * er;
        }
        check(10 * std::log10(err / sig) < -120, "room: свёртка = FIR, ambience " + std::to_string(int(amb)));
    }
}

void roomOffAndSwitch() {
    RoomStage st(48000);
    const auto l = rnoise(B * 40, 3), r = rnoise(B * 40, 4);
    const Pair o = runRoom(st, l, r);
    check(o.l == l && o.r == r, "room: Off — бит в бит");
    const auto i0 = ir(0);
    const Pair o2 = runRoom(st, l, r, [&](int b) {
        if (b == 0) st.set(std::make_unique<RoomConv>(*i0));
        if (b == 12) st.set(nullptr);
    });
    check(std::equal(o2.l.end() - B * 10, o2.l.end(), l.end() - B * 10), "room: после Off — бит в бит");
    st.drain();
    const int n = B * 400;
    const double hz = 300;
    std::vector<float> s(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) s[size_t(i)] = float(0.3 * std::sin(2 * M_PI * hz * double(i) / 48000));
    const std::shared_ptr<RoomIR> irs[2] = {ir(0), ir(1)};
    RoomStage sw(48000);
    const Pair o3 = runRoom(sw, s, s, [&](int b) {
        if (b >= 300 || b % 3 != 0) return;
        if (b % 7 == 0)
            sw.set(nullptr);
        else
            sw.set(std::make_unique<RoomConv>(*irs[b % 2]));
        sw.ambience.store(float(b % 5) / 4);
        sw.drain();
    });
    float jump = 0;
    for (size_t i = 1; i < o3.l.size(); ++i) jump = std::max(jump, std::fabs(o3.l[i] - o3.l[i - 1]));
    check(jump < float(0.3 * 2 * M_PI * hz / 48000) * 8, "room: смена комнат без разрывов");
}

std::vector<float> pink(int n) {
    int size = 1024;
    while (size < n) size *= 2;
    PvFFT fft(size);
    const int bins = size / 2 + 1;
    std::vector<float> re(static_cast<size_t>(bins), 0.f), im(static_cast<size_t>(bins), 0.f),
        t(static_cast<size_t>(size));
    uint64_t x = 99;
    for (int k = 1; k < bins; ++k) {
        const double hz = double(k) * 48000 / double(size);
        if (hz < 20 || hz > 20000) continue;
        x = x * 6364136223846793005ull + 1442695040888963407ull;
        const double ph = double(x >> 11) / double(1ull << 53) * 2 * M_PI;
        const double m = 1 / std::sqrt(hz);
        re[size_t(k)] = float(m * std::cos(ph));
        im[size_t(k)] = float(m * std::sin(ph));
    }
    fft.inverse(re.data(), im.data(), t.data());
    double e = 0;
    for (int i = 0; i < n; ++i) e += double(t[size_t(i)]) * t[size_t(i)];
    const float g = float(0.1 / std::sqrt(e / double(n)));
    std::vector<float> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out[size_t(i)] = t[size_t(i)] * g;
    return out;
}

void levelCompensation() {
    const int n = 48000 * 4;
    const auto pk = pink(n);
    bool ok = true;
    for (int k = 0; k < 2; ++k) {
        const auto x = ir(k);
        const RoomResponse resp = x->response(48000);
        for (double amb : {0.0, 0.5, 1.0}) {
            const auto p = MonitorDesign::make(nullptr, MonitorSettings(), nullptr, nullptr, &resp, amb, 48000);
            MonitorChain c(48000);
            c.publish(p);
            c.room.ambience.store(float(amb));
            c.room.set(std::make_unique<RoomConv>(*x));
            std::vector<float> bl(B), br(B);
            double e = 0, e0 = 0;
            for (int at = 0; at + B <= n; at += B) {
                for (int i = 0; i < B; ++i) bl[size_t(i)] = br[size_t(i)] = pk[size_t(at + i)];
                c.process(bl.data(), br.data(), B, false);
                if (at > 48000)
                    for (int i = 0; i < B; ++i) {
                        e += double(bl[size_t(i)]) * bl[size_t(i)] + double(br[size_t(i)]) * br[size_t(i)];
                        e0 += 2 * double(pk[size_t(at + i)]) * pk[size_t(at + i)];
                    }
            }
            ok = ok && near(10 * std::log10(e / e0), 0, 1.0);
        }
    }
    check(ok, "room: компенсация громкости ±1 dB");
}

void latency() {
    const auto x = ir(1);
    RoomStage st(48000);
    st.ambience.store(1);
    st.set(std::make_unique<RoomConv>(*x));
    std::vector<float> imp(B * 40, 0.f), z(B * 40, 0.f);
    imp[B * 20] = 1;
    const Pair o = runRoom(st, imp, z);
    int first = -1;
    for (size_t i = 0; i < o.l.size(); ++i)
        if (std::fabs(o.l[i]) > 1e-6f) {
            first = int(i);
            break;
        }
    int irFirst = 0;
    for (int i = 0; i < x->frames; ++i)
        if (std::fabs(x->h[size_t(i)]) > 1e-6f) {
            irFirst = i;
            break;
        }
    check(first - B * 20 - irFirst == 128, "room: задержка 128");
}

// HEAD.
std::unique_ptr<RoomConv> headConv(int k) {
    const auto r = rooms()[size_t(k)];
    const auto h = RoomIR::fromWav(r->name, readBytes(res() + r->headFile), r->headGainDb, 0);
    return std::make_unique<RoomConv>(*ir(k), h.get(), r->headFrom, r->headStep);
}
Pair runHead(RoomStage& st, const std::vector<float>& l, const std::vector<float>& r,
             const std::function<std::optional<float>(int)>& yaw) {
    return runRoom(st, l, r, [&](int b) {
        const auto y = yaw(b);
        if (y) {
            st.headOn.store(1);
            st.headYaw.store(*y);
        } else {
            st.headOn.store(0);
        }
    });
}
double energy(const std::vector<float>& x, int from) {
    double e = 0;
    for (size_t i = size_t(from); i < x.size(); ++i) e += double(x[i]) * x[i];
    return e;
}

void head() {
    bool ok = true;
    for (int k = 0; k < 2; ++k) {
        const auto r = rooms()[size_t(k)];
        const auto h = RoomIR::fromWav(r->name, readBytes(res() + r->headFile), r->headGainDb, 0);
        ok = ok && h && h->frames == 51 * RoomConv::part && r->headFrom == -50 && r->headStep == 2;
    }
    check(ok, "head: наборы по углам");
    const int n = B * 40;
    const auto l = rnoise(n, 3), r = rnoise(n, 4);
    ok = true;
    for (int k = 0; k < 2; ++k) {
        RoomStage sa(48000), sb(48000);
        sa.ambience.store(0.5f);
        sb.ambience.store(0.5f);
        sa.set(headConv(k));
        sb.set(headConv(k));
        const Pair a = runHead(sa, l, r, [](int) { return std::nullopt; });
        const Pair b = runHead(sb, l, r, [](int) { return 0.f; });
        double err = 0;
        for (int i = 4096; i < n; ++i)
            err += std::pow(a.l[size_t(i)] - b.l[size_t(i)], 2) + std::pow(a.r[size_t(i)] - b.r[size_t(i)], 2);
        ok = ok && 10 * std::log10(err / (energy(a.l, 4096) + energy(a.r, 4096))) < -60;
    }
    check(ok, "head: голова прямо — как без HEAD");
    const auto ln = rnoise(n, 5);
    const std::vector<float> zz(static_cast<size_t>(n), 0.f);
    auto ratio = [&](float y) {
        RoomStage st(48000);
        st.ambience.store(0);
        st.set(headConv(0));
        const Pair o = runHead(st, ln, zz, [y](int) { return y; });
        return 10 * std::log10(energy(o.l, 8192) / energy(o.r, 8192));
    };
    const double left = ratio(-30), mid = ratio(0), right = ratio(30);
    check(std::fabs(left) < 2.5 && mid > left + 3 && right > mid + 3, "head: направление поворота");
    const int m = B * 160;
    std::vector<float> s(static_cast<size_t>(m));
    for (int i = 0; i < m; ++i) s[size_t(i)] = 0.3f * float(std::sin(2 * M_PI * 1000 * double(i) / 48000));
    auto maxStep = [&](const Pair& o) {
        float mm = 0;
        for (int i = 8193; i < m; ++i)
            mm = std::max({mm, std::fabs(o.l[size_t(i)] - o.l[size_t(i - 1)]),
                           std::fabs(o.r[size_t(i)] - o.r[size_t(i - 1)])});
        return mm;
    };
    float still = 0;
    for (float y : {-50.f, 0.f, 50.f}) {
        RoomStage st(48000);
        st.ambience.store(0);
        st.set(headConv(0));
        still = std::max(still, maxStep(runHead(st, s, s, [y](int) { return y; })));
    }
    RoomStage sw(48000);
    sw.ambience.store(0);
    sw.set(headConv(0));
    const float moving = maxStep(runHead(sw, s, s, [](int b) {
        const float t = float(b - 40) / 94;
        return -50 + 100 * std::min(1.f, std::max(0.f, t));
    }));
    check(moving < still * 1.15f, "head: быстрый поворот — плавно");
    const int n2 = B * 60;
    const auto l2 = rnoise(n2, 7), r2 = rnoise(n2, 8);
    RoomStage s1(48000), s2(48000);
    s1.ambience.store(0);
    s2.ambience.store(0);
    s1.set(headConv(0));
    s2.set(headConv(0));
    const Pair a = runHead(s1, l2, r2, [](int) { return std::nullopt; });
    const Pair b = runHead(s2, l2, r2, [](int blk) { return blk < 20 ? std::optional<float>(40) : std::nullopt; });
    double err = 0;
    for (int i = B * 40; i < n2; ++i) err += std::pow(a.l[size_t(i)] - b.l[size_t(i)], 2);
    check(10 * std::log10(err / energy(a.l, B * 40)) < -60, "head: выключение — как без HEAD");
}
}  // namespace

int runMonitorTests() {
    neutralAndBypass();
    correctionResponse();
    limiter();
    sampleRates();
    brokenAndSmooth();
    roomsAndConv();
    roomOffAndSwitch();
    levelCompensation();
    latency();
    head();
    std::printf("monitor: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
