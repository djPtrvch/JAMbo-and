// Референс-трек аранжировщика (порт Core/Engine/RefPlayer.swift).
#include "RefPlayer.h"
#include <algorithm>
#include <cmath>

namespace jambo {

RefPlayer::RefPlayer()
    : outL(maxFrames, 0.f), outR(maxFrames, 0.f), rawL(maxFrames, 0.f), rawR(maxFrames, 0.f), post(maxFrames, 0.f) {
    throughMonitor.store(1);
}

int RefPlayer::frame(const SoundData& d, double x, double bpm) {
    const double sec = x * 15 / std::max(1.0, bpm);
    const double f = sec * 48000.0 - d.refOffset;
    return int(std::round(f));
}

void RefPlayer::render(const SoundData& d, bool play, double x, double bpm, int n) {
    std::fill(outL.begin(), outL.begin() + n, 0.f);
    std::fill(outR.begin(), outR.begin() + n, 0.f);
    std::fill(rawL.begin(), rawL.begin() + n, 0.f);
    std::fill(rawR.begin(), rawR.begin() + n, 0.f);
    const bool has = d.refLeft != nullptr && d.refFrames > 0;
    const bool same = d.refLeft == lastLeft;
    const bool want = play && has;
    if (!want && !active) return;
    const float g = d.refGain;
    if (!same) {
        active = false;
        lastLeft = d.refLeft;
    }
    if (!want) {
        add(d, pos, n, g, 1, -1 / float(n));
        active = false;
        std::copy(outL.begin(), outL.begin() + n, rawL.begin());
        std::copy(outR.begin(), outR.begin() + n, rawR.begin());
        return;
    }
    const int target = frame(d, x, bpm);
    if (!active) {
        pos = target;
        add(d, pos, n, g, 0, 1 / float(n));
        active = true;
    } else {
        const int dist = target > pos ? target - pos : pos - target;
        if (dist > tolerance) {
            add(d, pos, n, g, 1, -1 / float(n));
            pos = target;
            add(d, pos, n, g, 0, 1 / float(n));
        } else {
            add(d, pos, n, g, 1, 0);
        }
    }
    pos += n;
    std::copy(outL.begin(), outL.begin() + n, rawL.begin());
    std::copy(outR.begin(), outR.begin() + n, rawR.begin());
    applyMatch(n);
}

void RefPlayer::applyMatch(int n) {
    const float m1 = matchGain.load();
    const float m0 = match;
    if (m0 == 1 && m1 == 1) return;
    const float dm = (m1 - m0) / float(n);
    for (int k = 0; k < n; ++k) {
        const float m = m0 + dm * float(k + 1);
        outL[size_t(k)] *= m;
        outR[size_t(k)] *= m;
    }
    match = m1;
}

void RefPlayer::add(const SoundData& d, int from, int n, float g, float a, float da) {
    const float* l = d.refLeft;
    const float* r = d.refRight;
    if (!l || !r) return;
    const int frames = d.refFrames;
    float e = a;
    for (int k = 0; k < n; ++k) {
        const int i = from + k;
        if (i >= 0 && i < frames) {
            const float m = g * e;
            outL[size_t(k)] += l[i] * m;
            outR[size_t(k)] += r[i] * m;
        }
        e += da;
    }
}

void RefPlayer::mixPre(bool refB, float* left, float* right, int n, Levels& levels) {
    const float target = refB ? 1.f : 0.f;
    const float thruTarget = throughMaster.load() != 0 ? 1.f : 0.f;
    const float step = 1 / abFrames;
    float a = ab, m = thru, pl = 0, pr = 0;
    for (int k = 0; k < n; ++k) {
        if (a < target) {
            a += step;
            if (a > target) a = target;
        } else if (a > target) {
            a -= step;
            if (a < target) a = target;
        }
        if (m < thruTarget) {
            m += step;
            if (m > thruTarget) m = thruTarget;
        } else if (m > thruTarget) {
            m -= step;
            if (m < thruTarget) m = thruTarget;
        }
        const size_t K = size_t(k);
        const float ul = rawL[K], ur = rawR[K];
        const float al = ul < 0 ? -ul : ul;
        const float ar = ur < 0 ? -ur : ur;
        if (al > pl) pl = al;
        if (ar > pr) pr = ar;
        const float am = a * m;
        const float b = 1 - am;
        left[k] = left[k] * b + outL[K] * am;
        right[k] = right[k] * b + outR[K] * am;
        post[K] = a - am;
    }
    ab = a;
    thru = m;
    float* lv = levels.current(Levels::ref);
    lv[0] = pl;
    lv[1] = pr;
    if (levels.loudness.listens(Levels::ref)) levels.loudness.process(rawL.data(), rawR.data(), n);
}

void RefPlayer::mixPost(float* left, float* right, int n) {
    for (int k = 0; k < n; ++k) {
        const float p = post[size_t(k)];
        if (p == 0) continue;
        const float b = 1 - p;
        left[k] = left[k] * b + outL[size_t(k)] * p;
        right[k] = right[k] * b + outR[size_t(k)] * p;
    }
}

}  // namespace jambo
