// Порт Core/Engine/StemRing.swift (ветка swift JAMbo).
#include "StemRing.h"
#include <algorithm>

namespace jambo {

StemRing::StemRing()
    : gate(size_t(SeqData::maxTracks * maxFrames), 1.f), active(channels, 0), startF(channels, 0),
      endF(channels, never), count(channels, 0), dropped(channels, 0) {}

void StemRing::prepare() {
    if (ready.load(std::memory_order_acquire)) return;
    buf.reset(new float[size_t(capacity)]());
    ready.store(true, std::memory_order_release);
}

void StemRing::begin(int ch, int64_t at, int64_t limit) {
    const size_t c = size_t(ch);
    if (!isReady() || active[c]) return;
    active[c] = 1;
    startF[c] = at;
    endF[c] = limit == never ? never : at + limit;
    count[c] = 0;
    dropped[c] = 0;
    capturing_ = true;
    msgs.push(StemMsg{StemMsg::Kind::started, ch, 0, 0});
}

void StemRing::end(int ch, int64_t at) {
    const size_t c = size_t(ch);
    if (!active[c]) return;
    endF[c] = std::max(at, startF[c]);
}

void StemRing::captureBlock(int ch, const float* l, const float* r, const float* g, int64_t blockStart, int n) {
    const size_t c = size_t(ch);
    if (!active[c]) return;
    const int64_t a = std::max(startF[c], blockStart);
    const int64_t e = std::min(endF[c], blockStart + n);
    if (e > a && buf) {
        const int from = int(a - blockStart);
        const int k = int(e - a);
        const long w = written.load(std::memory_order_relaxed);
        const long rd = readTotal.load(std::memory_order_acquire);
        if (w + 2L * k - rd > capacity) {
            dropped[c] += k;
        } else {
            const long mask = capacity - 1;
            float* b = buf.get();
            long p = w;
            for (int i = 0; i < k; ++i) {
                const float gg = g ? g[from + i] : 1.f;
                b[p & mask] = g ? l[from + i] * gg : l[from + i];
                b[(p + 1) & mask] = g ? r[from + i] * gg : r[from + i];
                p += 2;
            }
            // Очередь полна — блок не считается записанным.
            if (msgs.push(StemMsg{StemMsg::Kind::data, ch, int64_t(k), w})) {
                written.store(p, std::memory_order_release);
                count[c] += k;
            } else {
                dropped[c] += k;
            }
        }
    }
    if (blockStart + n >= endF[c]) {
        active[c] = 0;
        msgs.push(StemMsg{StemMsg::Kind::ended, ch, count[c], dropped[c]});
        capturing_ = std::any_of(active.begin(), active.end(), [](uint8_t x) { return x != 0; });
    }
}

void StemRing::read(long pos, int k, std::vector<float>& l, std::vector<float>& r) {
    if (!buf) return;
    const long mask = capacity - 1;
    long p = pos;
    for (int i = 0; i < k; ++i) {
        l.push_back(buf[size_t(p & mask)]);
        r.push_back(buf[size_t((p + 1) & mask)]);
        p += 2;
    }
    readTotal.store(p, std::memory_order_release);
}

std::vector<StemCollector::Done> StemCollector::poll(StemRing& ring) {
    std::vector<Done> done;
    while (auto m = ring.msgs.pop()) {
        const int c = m->channel;
        const size_t ci = size_t(c);
        switch (m->kind) {
            case StemMsg::Kind::none: break;
            case StemMsg::Kind::started:
                left[ci].clear();
                right[ci].clear();
                recording_.insert(c);
                endAt_.erase(c);
                break;
            case StemMsg::Kind::data: ring.read(m->pos, int(m->value), left[ci], right[ci]); break;
            case StemMsg::Kind::endAt:
                if (m->value >= 0) endAt_[c] = m->value; else endAt_.erase(c);
                break;
            case StemMsg::Kind::ended:
                recording_.erase(c);
                endAt_.erase(c);
                if (!left[ci].empty()) done.push_back({c, std::make_shared<SampleData>(left[ci], &right[ci]), m->pos});
                left[ci].clear();
                right[ci].clear();
                break;
        }
    }
    return done;
}

}  // namespace jambo
