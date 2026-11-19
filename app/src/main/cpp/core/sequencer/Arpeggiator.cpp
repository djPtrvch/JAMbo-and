// Арпеджиатор в колбэке (порт Core/Sequencer/Arpeggiator.swift).
#include "Arpeggiator.h"
#include <algorithm>
#include <cmath>
#include "model/Arp.h"

namespace jambo {

Arpeggiator::Arpeggiator()
    : emitKind(maxEmit, 0), emitTrack(maxEmit, 0), emitNote(maxEmit, 0), emitVel(maxEmit, 1.f), emitFrame(maxEmit, 0) {
    const size_t n = size_t(tracks);
    const size_t h = n * maxHeld;
    on.assign(n, 0);
    dir.assign(n, 0);
    rate.assign(n, 1);
    octaves.assign(n, 1);
    gate.assign(n, 0.5);
    latch.assign(n, 0);
    heldNote.assign(h, 0);
    heldVel.assign(h, 1);
    heldId.assign(h, 0);
    heldPressed.assign(h, 0);
    heldCount.assign(n, 0);
    running.assign(n, 0);
    nextAt.assign(n, 0);
    index.assign(n, 0);
    sounding.assign(n, 0);
    offAt.assign(n, -1);
    lastNote.assign(n, -1);
    row.assign(maxRow, 0);
    rowVel.assign(maxRow, 1);
}

void Arpeggiator::configure(int t, bool o, int d, int r, int oc, double g, bool l, int64_t frame) {
    if (t < 0 || t >= tracks) return;
    const size_t T = size_t(t);
    dir[T] = int32_t(Arp::clampDir(d));
    rate[T] = Arp::rateSteps[Arp::clampRate(r)];
    octaves[T] = int32_t(Arp::clampOctaves(oc));
    gate[T] = Arp::clampGate(g);
    const bool wasOn = on[T] != 0;
    on[T] = o ? 1 : 0;
    latch[T] = l ? 1 : 0;
    if (wasOn && !o) {
        heldCount[T] = 0;
        stop(t, frame);
        return;
    }
    if (!l) dropReleased(t, frame);
}

void Arpeggiator::press(int t, int n, int id, float v, int64_t frame) {
    if (!active(t)) return;
    const size_t T = size_t(t);
    const int base = t * maxHeld;
    int c = int(heldCount[T]);
    if (latch[T]) {
        bool anyPressed = false;
        for (int i = 0; i < c; ++i)
            if (heldPressed[size_t(base + i)]) anyPressed = true;
        if (!anyPressed) {
            c = 0;
            heldCount[T] = 0;
        }
    }
    if (c >= maxHeld) return;
    heldNote[size_t(base + c)] = int32_t(n);
    heldVel[size_t(base + c)] = v;
    heldId[size_t(base + c)] = id;
    heldPressed[size_t(base + c)] = 1;
    heldCount[T] = int32_t(c + 1);
    if (!running[T]) {
        running[T] = 1;
        index[T] = 0;
        nextAt[T] = double(frame);
    }
}

void Arpeggiator::release(int t, int id, int64_t frame) {
    if (t < 0 || t >= tracks) return;
    const int base = t * maxHeld;
    const int c = int(heldCount[size_t(t)]);
    for (int i = 0; i < c; ++i)
        if (heldId[size_t(base + i)] == id) heldPressed[size_t(base + i)] = 0;
    if (!latch[size_t(t)]) dropReleased(t, frame);
}

void Arpeggiator::dropReleased(int t, int64_t frame) {
    const int base = t * maxHeld;
    const int c = int(heldCount[size_t(t)]);
    int k = 0;
    for (int i = 0; i < c; ++i) {
        const size_t I = size_t(base + i), K = size_t(base + k);
        if (!heldPressed[I]) continue;
        heldNote[K] = heldNote[I];
        heldVel[K] = heldVel[I];
        heldId[K] = heldId[I];
        heldPressed[K] = 1;
        ++k;
    }
    heldCount[size_t(t)] = int32_t(k);
    if (k == 0) stop(t, frame);
}

void Arpeggiator::stop(int t, int64_t frame) {
    const size_t T = size_t(t);
    if (sounding[T]) emit(0, t, lastNote[T], 1, frame);
    sounding[T] = 0;
    offAt[T] = -1;
    running[T] = 0;
    lastNote[T] = -1;
}

void Arpeggiator::emit(int32_t kind, int t, int32_t n, float v, int64_t f) {
    if (emitCount >= maxEmit) return;
    const size_t e = size_t(emitCount);
    emitKind[e] = kind;
    emitTrack[e] = int32_t(t);
    emitNote[e] = n;
    emitVel[e] = v;
    emitFrame[e] = f;
    ++emitCount;
}

int Arpeggiator::buildRow(int t) {
    const int base = t * maxHeld;
    const int c = int(heldCount[size_t(t)]);
    const int d = int(dir[size_t(t)]);
    int u = 0;
    for (int i = 0; i < c; ++i) {
        const int32_t n = heldNote[size_t(base + i)];
        bool dup = false;
        for (int j = 0; j < u; ++j)
            if (row[size_t(j)] == n) dup = true;
        if (!dup) {
            row[size_t(u)] = n;
            rowVel[size_t(u)] = heldVel[size_t(base + i)];
            ++u;
        }
    }
    if (d != Arp::order) {
        for (int a = 1; a < u; ++a) {
            const int32_t n = row[size_t(a)];
            const float v = rowVel[size_t(a)];
            int b = a - 1;
            while (b >= 0 && row[size_t(b)] > n) {
                row[size_t(b + 1)] = row[size_t(b)];
                rowVel[size_t(b + 1)] = rowVel[size_t(b)];
                --b;
            }
            row[size_t(b + 1)] = n;
            rowVel[size_t(b + 1)] = v;
        }
    }
    int len = u;
    const int oc = int(octaves[size_t(t)]);
    for (int o = 1; o < oc; ++o) {
        for (int k = 0; k < u && len < maxRow / 2; ++k) {
            const int32_t m = row[size_t(k)] + int32_t(12 * o);
            if (m <= 127) {
                row[size_t(len)] = m;
                rowVel[size_t(len)] = rowVel[size_t(k)];
                ++len;
            }
        }
    }
    return len;
}

int Arpeggiator::pick(int d, int k, int len) {
    if (len < 2) return 0;
    switch (d) {
    case Arp::down:
        return len - 1 - (k % len);
    case Arp::upDown: {
        const int p = 2 * len - 2;
        const int r = k % p;
        return r < len ? r : p - r;
    }
    case Arp::downUp: {
        const int p = 2 * len - 2;
        const int r = k % p;
        return r < len ? len - 1 - r : r - len + 1;
    }
    case Arp::random:
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return int(rng % uint64_t(len));
    default:
        return k % len;
    }
}

void Arpeggiator::process(int64_t start, int frames, bool playing, int nextTick, double nextFrame,
                          double stepFrames) {
    const double end = double(start + int64_t(frames));
    for (int t = 0; t < tracks; ++t) {
        const size_t T = size_t(t);
        if (!on[T] || !running[T]) continue;
        for (int guardN = 0; guardN < 64; ++guardN) {
            const int64_t off = offAt[T];
            const double at = nextAt[T];
            if (sounding[T] && off >= 0 && double(off) <= at && double(off) < end) {
                emit(0, t, lastNote[T], 1, off);
                sounding[T] = 0;
                offAt[T] = -1;
                continue;
            }
            if (at >= end) break;
            const int len = buildRow(t);
            if (len == 0) {
                stop(t, int64_t(at));
                break;
            }
            if (sounding[T]) {
                emit(0, t, lastNote[T], 1, int64_t(at));
                sounding[T] = 0;
            }
            const int k = pick(int(dir[T]), int(index[T]), len);
            const int32_t note = row[size_t(k)];
            const float vel = rowVel[size_t(k)];
            const int64_t f = int64_t(at);
            emit(1, t, note, vel, f);
            const double period = rate[T] * stepFrames;
            const double gf = std::max(1.0, gate[T] * period);
            sounding[T] = 1;
            lastNote[T] = note;
            offAt[T] = f + int64_t(gf);
            index[T] = int32_t((int(index[T]) + 1) % 1000000);
            ArpOut o;
            o.track = int32_t(t);
            o.note = note;
            o.velocity = vel;
            o.frame = at;
            o.gateFrames = gf;
            o.playing = playing;
            o.nextTick = nextTick;
            o.nextFrame = nextFrame;
            o.stepFrames = stepFrames;
            out.push(o);
            if (playing && stepFrames > 0) {
                const double pos = double(nextTick) - (nextFrame - at) / stepFrames;
                const double r = rate[T];
                const double g = std::floor((pos / r) + 0.000001) + 1;
                nextAt[T] = nextFrame - (double(nextTick) - g * r) * stepFrames;
            } else {
                nextAt[T] = at + period;
            }
        }
    }
}

}  // namespace jambo
