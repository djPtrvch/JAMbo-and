// Арпеджиатор в колбэке: порт ArpTests (движок) ветки swift JAMbo.
#include "model/Arp.h"
#include "sequencer/Arpeggiator.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
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
struct Run {
    std::vector<std::pair<int64_t, int32_t>> on;
    std::vector<int64_t> off;
};
Run run(Arpeggiator& a, int blocks, bool playing = false) {
    Run r;
    for (int b = 0; b < blocks; ++b) {
        a.process(int64_t(b * 256), 256, playing, 0, 0, 6000);
        for (int i = 0; i < a.emitCount; ++i) {
            if (a.emitKind[size_t(i)] == 1)
                r.on.push_back({a.emitFrame[size_t(i)], a.emitNote[size_t(i)]});
            else
                r.off.push_back(a.emitFrame[size_t(i)]);
        }
        a.clearEmit();
    }
    return r;
}

void freeRun() {
    Arpeggiator a;
    a.configure(0, true, Arp::up, 2, 1, 0.5, false, 0);
    a.press(0, 64, 1, 1, 0);
    a.press(0, 60, 2, 1, 0);
    const Run r = run(a, 100);
    std::vector<int64_t> f;
    std::vector<int32_t> n;
    for (const auto& x : r.on) {
        f.push_back(x.first);
        n.push_back(x.second);
    }
    check(f == std::vector<int64_t>{0, 6000, 12000, 18000, 24000} && n == std::vector<int32_t>{60, 64, 60, 64, 60},
          "arp: Up по темпу");
    check(r.off.size() >= 2 && r.off[0] == 3000 && r.off[1] == 9000, "arp: GATE 50 %");
}

void releaseAndLatch() {
    Arpeggiator a;
    a.configure(0, true, Arp::up, 2, 1, 1, false, 0);
    a.press(0, 60, 1, 1, 0);
    run(a, 4);
    a.release(0, 1, 1024);
    check(a.emitCount == 1, "arp: отпустили — нота гаснет");
    a.clearEmit();
    check(run(a, 100).on.empty(), "arp: без нот — стоит");
    Arpeggiator b;
    b.configure(2, true, Arp::up, 2, 1, 0.5, true, 0);
    b.press(2, 60, 1, 1, 0);
    b.release(2, 1, 10);
    check(run(b, 50).on.size() == 3, "arp: LATCH держит");
    b.press(2, 70, 5, 1, 12800);
    const Run r = run(b, 50);
    check(std::all_of(r.on.begin(), r.on.end(), [](const auto& x) { return x.second == 70; }), "arp: набор заменён");
    b.configure(2, true, Arp::up, 2, 1, 0.5, false, 0);
    check(b.emitCount == 0, "arp: палец держит — играет");
    b.release(2, 5, 0);
    b.clearEmit();
    check(run(b, 50).on.empty(), "arp: сняли LATCH и отпустили — стоп");
}

void gridWhenPlaying() {
    Arpeggiator a;
    a.configure(1, true, Arp::up, 0, 1, 0.5, false, 0);
    a.press(1, 60, 1, 0.5f, 1000);
    std::vector<int64_t> on;
    for (int b = 0; b < 120; ++b) {
        a.process(int64_t(b * 256), 256, true, 0, 0, 6000);
        for (int i = 0; i < a.emitCount; ++i)
            if (a.emitKind[size_t(i)] == 1) on.push_back(a.emitFrame[size_t(i)]);
        a.clearEmit();
    }
    check(on == std::vector<int64_t>{1000, 12000, 24000}, "arp: при игре — на сетке");
    std::vector<ArpOut> outs;
    while (auto o = a.out.pop()) outs.push_back(*o);
    check(outs.size() == 3 && outs[0].velocity == 0.5f && outs[1].gateFrames == 6000, "arp: ноты для записи");
}

void directions() {
    Arpeggiator a;
    a.configure(0, true, Arp::upDown, 4, 2, 0.5, false, 0);
    a.press(0, 60, 1, 1, 0);
    a.press(0, 64, 2, 1, 0);
    const Run r = run(a, 100);
    std::vector<int32_t> n;
    for (size_t i = 0; i < 8 && i < r.on.size(); ++i) n.push_back(r.on[i].second);
    check(n == std::vector<int32_t>{60, 64, 72, 76, 72, 64, 60, 64}, "arp: Up-Down, 2 октавы");
    Arpeggiator b;
    b.configure(0, true, Arp::random, 4, 1, 0.5, false, 0);
    b.press(0, 60, 1, 1, 0);
    b.press(0, 62, 2, 1, 0);
    b.press(0, 64, 3, 1, 0);
    std::set<int32_t> s;
    for (const auto& x : run(b, 200).on) s.insert(x.second);
    check(s == std::set<int32_t>{60, 62, 64}, "arp: Random");
}
}  // namespace

int runArpTests() {
    freeRun();
    releaseAndLatch();
    gridWhenPlaying();
    directions();
    std::printf("arp: %s (%d checks)\n", fails == 0 ? "all passed" : "FAILED", checks);
    return fails;
}
