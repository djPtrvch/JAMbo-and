// Порт Core/Sequencer/SequencerStems.swift (ветка swift JAMbo): запись стемов в колбэке на каждом шаге — с точностью до
// отсчёта. ● готовит канал; запись готовых и играющих — с PLAY; строки по сетке — всё, как играет, конец — когда строки
// пошли по кругу или кончились; без строк клип по кругу пишется один раз; дорожка остановилась — конец там же.
#include <cmath>
#include "Sequencer.h"

namespace jambo {

namespace {
uint16_t cellBit(int slot) { return uint16_t(1u << std::min(15, slot)); }
}  // namespace

void Sequencer::armStem(int ch, bool on) {
    if (ch >= 0 && ch < StemRing::channels) stemRules.armed[size_t(ch)] = on ? 1 : 0;
}

void Sequencer::finishStem(int ch) {
    if (ch < 0 || ch >= StemRing::channels) return;
    stemRules.armed[size_t(ch)] = 0;
    if (stemRules.rec[size_t(ch)]) stemRules.finish[size_t(ch)] = 1;
}

bool Sequencer::requestStemStop() {
    if (!playing_ || !stemRules.anyRecording()) return false;
    stemRules.stopAsked = true;
    return true;
}

bool Sequencer::stemsStep(const SeqData& d, double time) {
    StemRules& s = stemRules;
    const int64_t at = int64_t(std::floor(time));
    if (tick_ == 0) stemsStart(d, at);
    const bool arranged = stemsArranged(d);
    const bool wrapped = launcher.wrappedAt == tick_;
    const int m = StemRing::master;
    if (s.rec[size_t(m)]) {
        if (!stemsAnyPlaying(d)) {
            stemEnd(m, at);
        } else if (stemsMasterEdge(d, tick_) && tick_ > s.startTick[size_t(m)]) {
            if (s.stopAsked || s.finish[size_t(m)] || (arranged ? wrapped : stemsMasterAllSeen(d))) {
                stemEnd(m, at);
            } else {
                stemsMasterSee(d);
            }
        }
    }
    for (int t = 0; t < SeqData::maxTracks; ++t) {
        const size_t ti = size_t(t);
        if (!s.rec[ti]) continue;
        if (t >= d.trackCount || !launcher.plays(d, t)) {
            stemEnd(t, at);
            continue;
        }
        const int len = d.tickLen(t, launcher.slot[ti]);
        const bool boundary = len > 0 && (tick_ - launcher.trackStart[ti]) % len == 0;
        if (boundary && tick_ > s.startTick[ti]) {
            const uint16_t bit = cellBit(launcher.slot[ti]);
            if (s.stopAsked || s.finish[ti] || (arranged ? wrapped : (s.seen[ti] & bit) != 0)) {
                stemEnd(t, at);
            } else {
                s.seen[ti] |= bit;
            }
        }
    }
    stemsPredict(d, arranged);
    if (s.stopAsked && !s.anyRecording()) {
        s.stopAsked = false;
        return true;
    }
    return false;
}

void Sequencer::stemsStopped(int64_t at) {
    for (int c = 0; c < StemRing::channels; ++c)
        if (stemRules.rec[size_t(c)]) stemEnd(c, at);
    stemRules.stopAsked = false;
}

void Sequencer::stemsStart(const SeqData& d, int64_t at) {
    StemRules& s = stemRules;
    if (!stemRing.isReady()) return;
    const int m = StemRing::master;
    if (s.armed[size_t(m)] && stemsAnyPlaying(d)) {
        stemBegin(m, at);
        std::fill(s.masterSeen.begin(), s.masterSeen.end(), 0);
        stemsMasterSee(d);
    }
    for (int t = 0; t < d.trackCount; ++t) {
        if (s.armed[size_t(t)] && launcher.plays(d, t)) {
            stemBegin(t, at);
            s.seen[size_t(t)] = cellBit(launcher.slot[size_t(t)]);
        }
    }
}

void Sequencer::stemBegin(int ch, int64_t at) {
    stemRules.rec[size_t(ch)] = 1;
    stemRules.finish[size_t(ch)] = 0;
    stemRules.startTick[size_t(ch)] = tick_;
    stemRules.lastEnd[size_t(ch)] = -1;
    stemRing.begin(ch, at);
}

void Sequencer::stemEnd(int ch, int64_t at) {
    stemRules.rec[size_t(ch)] = 0;
    stemRules.finish[size_t(ch)] = 0;
    stemRing.end(ch, at);
}

bool Sequencer::stemsArranged(const SeqData& d) const {
    const int row = launcher.sceneRow;
    if (row < 0) return false;
    return d.repeats[size_t(std::min(row, SeqData::slots - 1))] > 0 && !d.rowLoop && d.rows > 1;
}

bool Sequencer::stemsAnyPlaying(const SeqData& d) const {
    for (int t = 0; t < d.trackCount; ++t)
        if (launcher.plays(d, t)) return true;
    return false;
}

void Sequencer::stemsMasterSee(const SeqData& d) {
    for (int t = 0; t < d.trackCount; ++t)
        if (launcher.plays(d, t)) stemRules.masterSeen[size_t(t)] |= cellBit(launcher.slot[size_t(t)]);
}

bool Sequencer::stemsMasterAllSeen(const SeqData& d) const {
    for (int t = 0; t < d.trackCount; ++t)
        if (launcher.plays(d, t) && (stemRules.masterSeen[size_t(t)] & cellBit(launcher.slot[size_t(t)])) == 0)
            return false;
    return true;
}

bool Sequencer::stemsMasterEdge(const SeqData& d, int k) const {
    if (launcher.sceneRow >= 0) {
        const int len = launcher.sceneLen;
        return len > 0 && (k - launcher.sceneStart) % len == 0;
    }
    int best = -1, bestLen = 0;
    for (int t = 0; t < d.trackCount; ++t) {
        if (!launcher.plays(d, t)) continue;
        const int len = d.tickLen(t, launcher.slot[size_t(t)]);
        if (best < 0 || len > bestLen) {
            best = t;
            bestLen = len;
        }
    }
    return best >= 0 && bestLen > 0 && (k - launcher.trackStart[size_t(best)]) % bestLen == 0;
}

void Sequencer::stemsPredict(const SeqData& d, bool arranged) {
    for (int c = 0; c < StemRing::channels; ++c) {
        if (!stemRules.rec[size_t(c)]) continue;
        const int k = c == StemRing::master ? stemsMasterEndTick(d, arranged) : stemsTrackEndTick(d, c, arranged);
        const int64_t f = k >= 0 ? int64_t(std::floor(stepFrame(k))) : -1;
        if (f != stemRules.lastEnd[size_t(c)]) {
            stemRules.lastEnd[size_t(c)] = f;
            stemRing.endAt(c, f);
        }
    }
}

int Sequencer::stemsTrackEndTick(const SeqData& d, int t, bool arranged) const {
    if (t >= d.trackCount || !launcher.plays(d, t)) return -1;
    const StemRules& s = stemRules;
    const size_t ti = size_t(t);
    const int len = d.tickLen(t, launcher.slot[ti]);
    if (len <= 0) return -1;
    const int start = launcher.trackStart[ti];
    const int at = start + ((tick_ - start) / len + 1) * len;
    const int q = launcher.queued[ti];
    if (s.stopAsked || s.finish[ti] || q == Launcher::stop) return at;
    if (arranged) {
        const int row = launcher.sceneRow;
        if (row + 1 >= d.rows)
            return launcher.sceneStart + d.repeats[size_t(std::min(row, SeqData::slots - 1))] * launcher.sceneLen;
        return -1;
    }
    if (q >= 0) return (s.seen[ti] & uint16_t(1u << q)) != 0 ? at : -1;
    const bool sceneChanges = launcher.sceneQueued >= 0 || (launcher.sceneRow >= 0 && launcher.sceneEnds(d, at));
    return sceneChanges ? -1 : at;
}

int Sequencer::stemsMasterEndTick(const SeqData& d, bool arranged) const {
    const StemRules& s = stemRules;
    const int m = StemRing::master;
    if (!(s.stopAsked || s.finish[size_t(m)] || (!arranged && stemsMasterAllSeen(d)))) return -1;
    for (int k = tick_ + 1; k < tick_ + 4096; ++k)
        if (stemsMasterEdge(d, k)) return k;
    return -1;
}

}  // namespace jambo
