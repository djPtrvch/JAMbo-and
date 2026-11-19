// Порт Core/Sequencer/Launcher.swift и ArrangePlay.swift (ветка swift JAMbo).
#include "Launcher.h"
#include <algorithm>
#include "TempoSource.h"

namespace jambo {

namespace {
template <typename T>
void rotate(T* p, int a, int b) {
    T x = p[a];
    if (a < b) {
        for (int i = a; i < b; ++i) p[i] = p[i + 1];
    } else {
        for (int i = a; i > b; --i) p[i] = p[i - 1];
    }
    p[b] = x;
}
}  // namespace

Launcher::Launcher(SPSCQueue<SeqOut>& o) : out(o) {
    const size_t n = SeqData::maxTracks;
    slot.assign(n, 0);
    stopped.assign(n, 0);
    queued.assign(n, none);
    trackStart.assign(n, 0);
    follow.reset(new bool[n]());
    arrDirty.assign(n, 0);
}

void Launcher::adopt(const SeqData& d) {
    for (int t = 0; t < SeqData::maxTracks; ++t) {
        slot[size_t(t)] = t < d.trackCount ? d.slot[size_t(t)] : 0;
        stopped[size_t(t)] = t >= d.trackCount || d.stopped[size_t(t)] != 0;
    }
}

void Launcher::move(int from, int to) {
    const int n = SeqData::maxTracks;
    if (from == to || from < 0 || to < 0 || from >= n || to >= n) return;
    rotate(slot.data(), from, to);
    rotate(stopped.data(), from, to);
    rotate(queued.data(), from, to);
    rotate(trackStart.data(), from, to);
    rotate(follow.get(), from, to);
    rotate(arrDirty.data(), from, to);
}

void Launcher::clearQueues() {
    for (int t = 0; t < SeqData::maxTracks; ++t) {
        if (queued[size_t(t)] == none) continue;
        queued[size_t(t)] = none;
        out.push(SeqOut::queue(t, none));
    }
    sceneRow = -1;
    sceneQueued = -1;
    out.push(SeqOut::scene(-1, 0, 16));
    out.push(SeqOut::sceneQueue(-1));
}

void Launcher::reset() {
    for (int t = 0; t < SeqData::maxTracks; ++t) {
        queued[size_t(t)] = none;
        trackStart[size_t(t)] = 0;
    }
    sceneRow = -1;
    sceneQueued = -1;
    sceneAtPass = false;
    wrappedAt = -1;
}

void Launcher::start(const SeqData& d) {
    arrPos = arrFrom;
    if (arrangeMode) {
        for (int t = 0; t < SeqData::maxTracks; ++t) queued[size_t(t)] = none;
        setFollowAll(true, true);
        return;
    }
    setFollowAll(false, true);
    bool anyQueued = false;
    for (int t = 0; t < d.trackCount; ++t) {
        if (queued[size_t(t)] == none) continue;
        anyQueued = true;
        apply(t, queued[size_t(t)], 0);
    }
    if (sceneQueued >= 0) {
        startScene(d, sceneQueued, 0);
    } else if (!anyQueued) {
        const int k = std::min(d.arrStart, d.rows - 1);
        if (rowFilled(d, k)) startScene(d, k, 0);
    }
}

void Launcher::queue(int t, int k) {
    queued[size_t(t)] = queued[size_t(t)] == k ? none : k;
    out.push(SeqOut::queue(t, queued[size_t(t)]));
}

void Launcher::queueScene(int k, bool atPass) {
    if (atPass) {
        sceneQueued = k;
        sceneAtPass = true;
    } else {
        sceneQueued = sceneQueued == k ? -1 : k;
        sceneAtPass = false;
    }
    out.push(SeqOut::sceneQueue(sceneQueued));
}

bool Launcher::beforeStep(const SeqData& d, int tick) {
    if (!arrangeStep(d, tick)) return false;
    if (sceneQueued >= 0 && sceneDue(tick)) {
        startScene(d, sceneQueued, tick);
        sceneAtPass = false;
    } else if (sceneRow >= 0 && sceneEnds(d, tick)) {
        if (sceneRow + 1 < d.rows) {
            startScene(d, sceneRow + 1, tick);
        } else if (d.arrEndStop) {
            return false;
        } else {
            startScene(d, std::min(d.arrStart, d.rows - 1), tick);
            wrappedAt = tick;
        }
    }
    for (int t = 0; t < d.trackCount; ++t)
        if (queued[size_t(t)] != none && atBoundary(d, t, tick)) apply(t, queued[size_t(t)], tick);
    for (int t = 0; t < d.trackCount; ++t)
        if (follow[size_t(t)]) syncArrange(d, t, tick);
    return true;
}

bool Launcher::rowFilled(const SeqData& d, int k) const {
    for (int t = 0; t < d.trackCount; ++t)
        if (d.filled(t, k)) return true;
    return false;
}

bool Launcher::sceneDue(int tick) const {
    if (sceneAtPass && sceneRow >= 0 && sceneLen > 0) return tick > sceneStart && (tick - sceneStart) % sceneLen == 0;
    return TempoGrid::isBarStart(double(tick) / TempoGrid::stepsPerBeat);
}

bool Launcher::sceneEnds(const SeqData& d, int tick) const {
    const int reps = d.repeats[size_t(std::max(0, std::min(SeqData::slots - 1, sceneRow)))];
    return reps > 0 && !d.rowLoop && d.rows > 1 && tick - sceneStart >= reps * sceneLen;
}

bool Launcher::atBoundary(const SeqData& d, int t, int tick) const {
    if (!plays(d, t)) return TempoGrid::isBarStart(double(tick) / TempoGrid::stepsPerBeat);
    return (tick - trackStart[size_t(t)]) % d.tickLen(t, slot[size_t(t)]) == 0;
}

void Launcher::apply(int t, int q, int tick) {
    queued[size_t(t)] = none;
    // Клип из сетки: дорожка уходит с таймлайна.
    setFollow(t, false);
    if (q == stop) {
        stopped[size_t(t)] = 1;
    } else {
        slot[size_t(t)] = q;
        stopped[size_t(t)] = 0;
    }
    trackStart[size_t(t)] = tick;
    out.push(SeqOut::clip(t, slot[size_t(t)], stopped[size_t(t)] != 0, tick));
}

void Launcher::startScene(const SeqData& d, int k, int at) {
    int len = 0;
    for (int t = 0; t < d.trackCount; ++t) {
        queued[size_t(t)] = none;
        setFollow(t, false);
        trackStart[size_t(t)] = at;
        if (d.filled(t, k)) {
            slot[size_t(t)] = k;
            stopped[size_t(t)] = 0;
            len = std::max(len, d.tickLen(t, k));
        } else {
            stopped[size_t(t)] = 1;
        }
        out.push(SeqOut::clip(t, slot[size_t(t)], stopped[size_t(t)] != 0, at));
    }
    sceneRow = k;
    sceneStart = at;
    sceneLen = len > 0 ? len : 16;
    sceneQueued = -1;
    sceneAtPass = false;
    out.push(SeqOut::scene(k, at, sceneLen));
    out.push(SeqOut::sceneQueue(-1));
}

// MARK: — аранжировка (ArrangePlay.swift)

void Launcher::setFollow(int t, bool on, bool force) {
    if (t < 0 || t >= SeqData::maxTracks) return;
    if (on) arrDirty[size_t(t)] = 1;
    if (follow[size_t(t)] == on && !force) return;
    follow[size_t(t)] = on;
    out.push(SeqOut::follow(t, on));
}

void Launcher::setFollowAll(bool on, bool force) {
    for (int t = 0; t < SeqData::maxTracks; ++t) setFollow(t, on, force);
}

void Launcher::returnToArrange(int t) {
    if (t >= 0 && t < SeqData::maxTracks && queued[size_t(t)] != none) {
        queued[size_t(t)] = none;
        out.push(SeqOut::queue(t, none));
    }
    setFollow(t, true);
}

void Launcher::seek(int pos) {
    arrPos = pos - 1;
    for (int t = 0; t < SeqData::maxTracks; ++t)
        if (follow[size_t(t)]) arrDirty[size_t(t)] = 1;
}

bool Launcher::arrangeStep(const SeqData& d, int tick) {
    if (tick > 0) {
        const int prev = arrPos;
        arrPos += 1;
        if (d.loop && prev < d.loopTo && arrPos >= d.loopTo && anyFollow(d)) arrPos = d.loopFrom;
    }
    out.push(SeqOut::arrange(arrPos));
    if (d.loop || d.arrEnd <= 0 || arrPos < d.arrEnd) return true;
    for (int t = 0; t < d.trackCount; ++t)
        if (!follow[size_t(t)]) return true;
    return false;
}

bool Launcher::anyFollow(const SeqData& d) const {
    for (int t = 0; t < d.trackCount; ++t)
        if (follow[size_t(t)]) return true;
    return false;
}

void Launcher::syncArrange(const SeqData& d, int t, int tick) {
    const int j = d.block(t, arrPos);
    int k = slot[size_t(t)];
    bool off = true;
    int start = trackStart[size_t(t)];
    if (j >= 0 && d.filled(t, d.blkClip[size_t(j)])) {
        k = d.blkClip[size_t(j)];
        off = false;
        start = tick - (arrPos - d.blkStart[size_t(j)] + d.blkOff[size_t(j)]);
    }
    if (!arrDirty[size_t(t)] && (stopped[size_t(t)] != 0) == off &&
        (off || (slot[size_t(t)] == k && trackStart[size_t(t)] == start)))
        return;
    arrDirty[size_t(t)] = 0;
    slot[size_t(t)] = k;
    stopped[size_t(t)] = off ? 1 : 0;
    trackStart[size_t(t)] = off ? tick : start;
    out.push(SeqOut::clip(t, k, off, trackStart[size_t(t)]));
}

}  // namespace jambo
