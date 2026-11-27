// Голоса и партии пяти стилей (StyleRoles.swift).
#include "StyleRhythm.h"
#include "Scales.h"
#include <climits>
#include <cmath>

namespace jambo::MelodyRhythm::Detail {
using namespace RhythmRandom;

namespace {

Ints voicing(int r, const Ints& offsets, int mode, int n, const Ints& prev, bool top) {
    if (mode < 3) {
        auto v = closeVoicing(r, offsets, n, prev.empty() ? std::nullopt
            : std::optional<int>(*std::max_element(prev.begin(), prev.end())));
        if (mode == 1 && v.size() >= 3) v[v.size() - 2] -= n;
        if (mode == 2) v.push_back(r - n);
        std::sort(v.begin(), v.end());
        return v;
    }
    const Ints upper(offsets.begin() + 1, offsets.end());
    Ints best{r};
    int bestDistance = INT_MAX;
    for (size_t rot = 0; rot < upper.size(); ++rot) {
        Ints row{r};
        int last = 0;
        for (size_t j = 0; j < upper.size(); ++j) {
            last = upper[(rot + j) % upper.size()];
            const int low = row.back();
            int x = r + last;
            while (x <= low) x += n;
            if (x - low < n && x + n <= r + 3 * n) x += n;
            row.push_back(x);
        }
        int distance = 0;
        if (!prev.empty()) {
            const bool disjoint = std::none_of(row.begin(), row.end(), [&](int x) {
                return std::find(prev.begin(), prev.end(), x) != prev.end();
            });
            distance = std::abs(row.back() - *std::max_element(prev.begin(), prev.end())) + (disjoint ? 3 : 0);
        }
        if (top && last != 4 && last != 8) distance += 100;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = std::move(row);
        }
    }
    return best;
}

Voices voices(const StyleContext& c, int kind, int mode) {
    Voices out;
    const auto& offsets = Scales::chordDegrees[size_t(kind)];
    Ints prev;
    for (int root : c.roots) {
        prev = voicing(root, offsets, mode, c.scaleSize, prev, c.style == 4);
        out.push_back(prev);
    }
    return out;
}

StyleNotes bass(const StyleContext& c, Rng& rng) {
    const int ps = c.style;
    if (ps == 4) {
        Voices rows;
        for (int r : c.roots) rows.push_back({r});
        auto out = hold(c, rows, Role::bass, rng);
        if (c.count() > 1 && real(rng) < 0.3) {
            const int at = (c.count() - 1) * per + 8;
            for (auto& n : out) if (n.step < at && n.step + n.length > at) n.length = at - n.step;
            out.push_back({at, {c.root(at) + 4}, per - 8, styleVelocity(Role::bass, ps, 8, false, rng)});
        }
        return out;
    }
    Ints steps;
    if (ps == 2) {
        for (int pi = 0; pi < c.count(); ++pi)
            for (int step : c.kicks[size_t(pi)]) {
                if (step < c.length(pi) && (pi < c.count() - 1 || c.count() == 1 || step < 12))
                    steps.push_back(pi * per + step);
            }
    } else {
        const StyleTable table(bassCells[ps]);
        steps = styleCells(table.rows[table.pick(rng)], c, ps == 0 ? 1 : 2, per, ps < 2, rng);
    }
    const StyleTable pitch(bassPitch[ps]);
    const int offsets[] = {0, c.scaleSize, 4, -1};
    constexpr int defaultLength[] = {1, 2, 16, 4};
    StyleNotes out;
    int lastSeg = -1, lastPage = -1;
    for (int s : steps) {
        const int si = int(c.segment(s)), k = c.roots[size_t(si)], st = s % per;
        int degree = k + offsets[pitch.rows[pitch.pick(rng)].front()];
        if (si != lastSeg || (ps == 2 && st == 0)) degree = k;
        if (c.count() > 1 && s / per == c.count() - 1 && ps != 2 && st >= (ps == 3 ? 15 : 14))
            degree = c.roots.front() + (coin(rng) ? 1 : -1);
        const int length = ps == 0 && real(rng) < 0.25 ? 2 : defaultLength[ps];
        out.push_back({s, {degree}, length, styleVelocity(Role::bass, ps, st, s / per != lastPage, rng)});
        lastSeg = si;
        lastPage = s / per;
    }
    fit(out);
    return out;
}

int nearest(int cur, int root, int n, int hi, bool preferRoot) {
    int best = cur, distance = INT_MAX;
    for (int x = 0; x <= hi; ++x) {
        const int r = Scales::wrap(x - root, n);
        if (r != 0 && (preferRoot || (r != 2 && r != 4))) continue;
        const int dx = std::abs(x - cur);
        if (dx < distance) { distance = dx; best = x; }
    }
    return best;
}

int walk(int cur, int hi, const std::vector<double>& moves, int& last, Rng& rng) {
    int m;
    if (std::abs(last) >= 3) m = last > 0 ? -1 : 1;
    else {
        const int k = int(weighted(rng, moves)) + 1;
        m = k < 3 ? k : integer(rng, 3, 4);
        if (coin(rng)) m = -m;
    }
    if (cur + m > hi || cur + m < 0) m = -m;
    last = m;
    return std::clamp(cur + m, 0, hi);
}

int snap(int d, const Ints& pool, int n, int hi) {
    if (pool.empty()) return d;
    for (int x : {d, d - 1, d + 1, d - 2, d + 2}) {
        if (x >= 0 && x <= hi && std::find(pool.begin(), pool.end(), Scales::wrap(x, n)) != pool.end()) return x;
    }
    return d;
}

StyleNotes lead(const StyleContext& c, Rng& rng) {
    const int ps = c.style, n = c.scaleSize;
    const StyleTable table(leadCells[ps]);
    const auto a = table.pick(rng), b = ps == 2 || ps == 4 ? a : table.pick(rng, int(a));
    const int hi = ps == 1 ? n + n / 2 : ps == 4 ? 2 * n : n;
    Ints pool;
    if (n == 7 && ps == 2) pool = {0, 2, 3, 4, 6};
    if (n == 7 && ps == 4) pool = {0, 1, 2, 4, 5};
    const std::vector<double> moves[] = {{60,30,10}, {55,35,10}, {60,30,10}, {70,20,10}, {50,50,0}};
    const int empty = ps == 4 && c.count() >= 3 && coin(rng) ? integer(rng, 1, c.count() - 2) : -1;
    Ints motifSteps, motif;
    int lastMove = 0, cur = std::min(hi, c.roots.front() + 2 * integer(rng, 0, 2)), leadSeg = -1;
    StyleNotes out;
    constexpr int normalLength[] = {1, 1, 2, 4, 16}, longLength[] = {2, 3, 6};
    for (int pi = 0; pi < c.count(); ++pi) {
        if (pi == empty) continue;
        const int form = ps == 2 ? (pi == 0 ? 0 : 2) : ps == 4 ? 3 : pi % 4;
        const int len = c.length(pi);
        const bool last = pi == c.count() - 1;
        Ints steps, made;
        for (int s : table.rows[form == 3 ? b : a]) {
            if (s < len && (ps >= 2 || !c.kicks[size_t(pi)].count(s))) steps.push_back(s);
        }
        if (ps == 3 && last && len > 12) {
            steps.erase(std::remove_if(steps.begin(), steps.end(), [](int s) { return s >= 12; }), steps.end());
            steps.push_back(12);
        }
        const bool endRoot = ps <= 1 ? form == 3 : last;
        for (size_t j = 0; j < steps.size(); ++j) {
            const int st = steps[j], s = pi * per + st, k = c.root(s);
            const int seg = int(c.segment(s));
            const auto m = std::find(motifSteps.begin(), motifSteps.end(), st);
            int d;
            if (m != motifSteps.end() && ((form == 0 || form == 2) || (form == 1 && j + 2 < steps.size()))) {
                d = std::clamp(k + motif[size_t(m - motifSteps.begin())], 0, hi);
            } else if (out.empty() || (ps == 4 && seg != leadSeg)) d = nearest(cur, k, n, hi, false);
            else d = snap(walk(cur, hi, moves[ps], lastMove, rng), pool, n, hi);
            if ((ps == 0 && st % 4 == 2) || (ps == 3 && st % 4 == 0)) d = nearest(d, k, n, hi, false);
            if (endRoot && j + 1 == steps.size()) d = nearest(d, k, n, hi, true);
            int length = ps < 3 && real(rng) < 0.3 ? longLength[ps] : normalLength[ps];
            if (ps == 3 && j + 1 == steps.size()) length = 8;
            out.push_back({s, {d}, length, styleVelocity(Role::lead, ps, st, j == 0, rng)});
            made.push_back(d - k);
            cur = d;
            leadSeg = seg;
        }
        if (form == 0 && motif.empty()) { motif = std::move(made); motifSteps = std::move(steps); }
    }
    fit(out);
    return out;
}

StyleNotes keys(const StyleContext& c, int kind, Rng& rng) {
    const int ps = c.style;
    int mode = ps == 4 ? 3 : 0;
    if (ps < 3 && coin(rng)) mode = ps == 2 ? 2 : 1;
    const auto rows = voices(c, kind, mode);
    const StyleTable table(keysCells[ps]);
    const auto choice = table.pick(rng);
    if (ps == 4 && choice == 0) return hold(c, rows, Role::keys, rng);
    StyleNotes out;
    if (ps == 4 && choice == 3) {
        for (int pi = 0; pi < c.count(); ++pi) {
            const auto& cell = table.rows[choice];
            for (size_t j = 0; j < cell.size(); ++j) {
                const int st = cell[j], s = pi * per + st;
                if (st >= c.length(pi)) continue;
                const auto& row = rows[c.segment(s)];
                out.push_back({s, {row[j % row.size()]}, per - st, styleVelocity(Role::keys, ps, st, false, rng)});
            }
        }
        return out;
    }
    constexpr int variation[] = {2,2,3,4,0}, cut[] = {12,14,12,14,16}, lengths[] = {1,1,16,1,16};
    const auto steps = styleCells(table.rows[choice], c, variation[ps], cut[ps], ps < 2, rng);
    int lastPage = -1;
    for (int s : steps) {
        int length = lengths[ps];
        const auto& ln = table.lens[choice];
        if (ps == 3 && ln.size() == 2) length = coin(rng) ? ln[0] : ln[1];
        if ((ps == 0 && real(rng) < 0.2) || (ps == 1 && coin(rng))) length = 2;
        out.push_back({s, rows[c.segment(s)], length, styleVelocity(Role::keys, ps, s % per, s / per != lastPage, rng)});
        lastPage = s / per;
    }
    fit(out);
    return out;
}

StyleNotes pad(const StyleContext& c, int kind, bool single, Rng& rng) {
    const int ps = c.style;
    int mode = ps == 2 || ps == 4 ? 3 : 0;
    if (ps == 1 && coin(rng)) mode = 2;
    Voices rows;
    if (single) for (int root : c.roots) rows.push_back({root});
    else rows = voices(c, kind, mode);
    return hold(c, rows, Role::pad, rng);
}

int figure(int f, int j, int w) {
    if (w < 2) return 0;
    const int skip = (j / 2 + (j % 2 == 1 ? 2 : 0)) % w;
    constexpr int alberti[] = {0, 2, 1, 2};
    const int wave = j % (2 * w - 2);
    switch (f) {
    case 1: return skip;
    case 2: return alberti[j % 4] % w;
    case 3: return w - 1 - skip;
    case 4: return wave < w ? wave : 2 * w - 2 - wave;
    default: return j % w;
    }
}

StyleNotes arp(const StyleContext& c, int kind, const TrackArp& a, Rng& rng) {
    const int ps = c.style, dir = Arp::clampDir(a.dir);
    constexpr int grid[] = {2, 2, 1, 1, 1};
    const int every = grid[Arp::clampRate(a.rate)];
    Voices rows;
    for (int root : c.roots) {
        Ints row;
        for (int offset : Scales::chordDegrees[size_t(kind)]) row.push_back(root + offset);
        rows.push_back(arpSequence(std::move(row), dir, a.octaves, c.scaleSize));
    }
    const StyleTable figures(arpFigures[ps]);
    const int f = figures.rows[figures.pick(rng)].front();
    Ints cell;
    if (*arpCells[ps]) {
        const StyleTable table(arpCells[ps]);
        for (int st : table.rows[table.pick(rng)]) if (st % every == 0) cell.push_back(st);
    } else {
        for (int st = 0; st < per; st += every) cell.push_back(st);
    }
    const int length = ps == 4 ? per : std::max(1, int(std::round(Arp::clampGate(a.gate) * every)));
    StyleNotes out;
    int j = 0, last = -1, fill = 0, cur = 0;
    for (int pi = 0; pi < c.count(); ++pi) {
        for (size_t h = 0; h < cell.size(); ++h) {
            const int st = cell[h], s = pi * per + st, si = int(c.segment(s));
            if (st >= c.length(pi)) continue;
            if (si != last) { last = si; j = 0; }
            const auto& row = rows[size_t(si)];
            if (row.empty()) continue;
            const bool end = c.count() > 1 && pi == c.count() - 1 && st >= 12;
            if (end && ps == 2 && ++fill % 2 == 0) continue;
            if (end && ps != 2 && ps != 4) ++cur;
            else cur = dir == Arp::random ? pick(rng, row) : row[size_t(figure(f, j, int(row.size())))];
            ++j;
            out.push_back({s, {cur}, length, styleVelocity(Role::arp, ps, st, h == 0, rng)});
        }
    }
    fit(out);
    return out;
}
}  // namespace

StyleNotes styleRole(const Part& part, const StyleContext& c, Rng& rng) {
    switch (part.role) {
    case Role::bass: return bass(c, rng);
    case Role::lead: return lead(c, rng);
    case Role::keys: return keys(c, part.kind, rng);
    case Role::pad: return pad(c, part.kind, part.single, rng);
    case Role::arp: return arp(c, part.kind, part.arp, rng);
    }
    return {};
}
}
