// Индикация мастера COMP | CLIP | LIM вне колбэка (порт DynBarsState и MasterMeters из MasterIndicator.swift, ветка swift
// JAMbo): IN и OUT — dB с мгновенным подъёмом и спадом 20 dB/s, красный держится не меньше 300 мс; GR — dB с тем же
// спадом; данные старше 150 мс — тишина.
#pragma once
#include <algorithm>
#include <cmath>
#include "master/DynTele.h"

namespace jambo {

class DynBarsState {
public:
    static constexpr float fall = 20;
    float inDb = -120, outDb = -120, gr = 0;
    double inOverUntil = 0, outOverUntil = 0;

    void tick(const DynTele& t, bool bypass, double now) {
        const float dt = last > 0 ? float(std::min(0.1, now - last)) : 0;
        last = now;
        const auto r = t.peaks(count);
        count = r.count;
        if (r.frames > 0) {
            mIn = r.inPeak;
            mGr = r.gr;
            mOut = r.outPeak;
            lastAt = now;
        }
        const bool live = now - lastAt < 0.15;
        const float vi = live ? db(mIn) : -120;
        const float vo = live ? db(mOut) : -120;
        inDb = std::max(vi, inDb - fall * dt);
        outDb = std::max(vo, outDb - fall * dt);
        if (vi > 0) inOverUntil = now + 0.3;
        if (vo > 0) outOverUntil = now + 0.3;
        const float g = live && !bypass ? mGr : 0;
        gr = bypass ? 0 : std::max(g, gr - fall * dt);
    }

    static float db(float x) { return 20 * std::log10(std::max(x, 1e-6f)); }

private:
    long count = 0;
    double lastAt = 0, last = 0;
    float mIn = 0, mGr = 0, mOut = 0;
};

struct MasterMeters {
    DynBarsState comp, clip, lim;
};

}  // namespace jambo
