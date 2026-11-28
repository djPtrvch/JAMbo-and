// Порт Core/Model/ArrangeEdits.swift и правок ArrBlock, Arrangement.repair (Arrange.swift; ветка swift JAMbo).
#include <climits>
#include <algorithm>
#include <cmath>
#include <iterator>
#include "Project.h"
#include "Track.h"

namespace jambo {

void ArrBlock::shiftAutos(int d) {
    if (d == 0 || autos.empty()) return;
    for (auto& l : autos) for (auto& p : l.points) p.x -= double(d);
}

void ArrBlock::pinAuto(const std::string& param, double x, double y) {
    for (auto& l : autos) {
        if (l.param != param) continue;
        for (const auto& p : l.points) if (p.x == x) return;
        auto it = std::find_if(l.points.begin(), l.points.end(), [x](const ArrAutoPoint& p) { return p.x > x; });
        l.points.insert(it, ArrAutoPoint{x, y});
        return;
    }
    autos.push_back({param, {ArrAutoPoint{x, y}}});
}

void ArrBlock::pinCut(int x) {
    const double cx = double(x);
    const std::vector<ArrAutoLane> ls = autos;  // Swift: обход копии
    for (const auto& l : ls)
        if (auto v = ArrAutoLane::value(l.points, cx)) pinAuto(l.param, cx, *v);
}

void Arrangement::repair() {
    for (auto& m : markers) {
        m.at = std::max(0, m.at);
        // prefix(12) — по символам: UTF-8 режется по границам символов.
        size_t i = 0, n = 0;
        while (i < m.name.size() && n < 12) {
            ++i;
            while (i < m.name.size() && (static_cast<unsigned char>(m.name[i]) & 0xC0) == 0x80) ++i;
            ++n;
        }
        m.name.resize(i);
        if (m.color < 0 || m.color >= ArrPalette::count) m.color = ArrPalette::defaultColor;
        if (m.end && *m.end <= m.at) m.end.reset();
    }
    std::stable_sort(markers.begin(), markers.end(), [](const ArrMarker& a, const ArrMarker& b) { return a.at < b.at; });
    loopFrom = std::max(0, loopFrom);
    if (loopTo <= loopFrom) {
        loopTo = loopFrom;
        loop = false;
    }
    grid = std::max(0, std::min(grid, 3));
    if (ref) {
        if (ref->key.empty()) {
            ref.reset();
        } else {
            ref->offset = std::isfinite(ref->offset) ? ref->offset : 0;
            ref->gain = std::isfinite(ref->gain) ? std::max(Params::volMin, std::min(0.0, ref->gain)) : 0;
            if (ref->bpm && (!std::isfinite(*ref->bpm) || *ref->bpm < 0)) ref->bpm.reset();
        }
    }
}

void Track::clearArrange(int from, int to) {
    if (to <= from) return;
    const auto first = std::lower_bound(blocks.begin(), blocks.end(), from,
        [](const ArrBlock& b, int x) { return b.end() <= x; });
    const auto last = std::lower_bound(first, blocks.end(), to,
        [](const ArrBlock& b, int x) { return b.start < x; });
    if (first == last) return;

    std::vector<ArrBlock> remain;
    if (first->start < from) {
        auto left = *first;
        left.length = from - left.start;
        left.pinCut(left.length);
        remain.push_back(std::move(left));
    }
    if (const auto& b = *std::prev(last); b.end() > to) {
        auto right = b;
        const int cut = to - b.start;
        right.pinCut(cut);
        right.start = to;
        right.length = b.end() - to;
        right.offset = b.offset + cut;
        right.shiftAutos(cut);
        remain.push_back(std::move(right));
    }
    const auto at = blocks.erase(first, last);
    blocks.insert(at, std::make_move_iterator(remain.begin()), std::make_move_iterator(remain.end()));
}

int Track::placeBlock(const ArrBlock& b) {
    ArrBlock n = b;
    n.start = std::max(0, n.start);
    n.length = std::max(1, n.length);
    if (blocks.empty() || blocks.back().end() <= n.start) {
        blocks.push_back(std::move(n));
        return int(blocks.size()) - 1;
    }
    clearArrange(n.start, n.end());
    const auto at = std::lower_bound(blocks.begin(), blocks.end(), n.start,
        [](const ArrBlock& existing, int x) { return existing.start < x; });
    return int(blocks.insert(at, std::move(n)) - blocks.begin());
}

void Track::repairBlocks() {
    if (color < 0 || color >= ArrPalette::count) color = ArrPalette::defaultColor;
    std::vector<ArrBlock> saved;
    for (const auto& b : blocks) if (b.clip >= 0 && b.clip < Params::patternSlots) saved.push_back(b);
    blocks.clear();
    std::stable_sort(saved.begin(), saved.end(), [](const ArrBlock& a, const ArrBlock& c) { return a.start < c.start; });
    for (const auto& b : saved) placeBlock(b);
}

// MARK: — остальные правки блоков дорожки (ArrangeEdits.swift)

namespace {
void sortBlocks(std::vector<ArrBlock>& b) {
    std::stable_sort(b.begin(), b.end(), [](const ArrBlock& a, const ArrBlock& c) { return a.start < c.start; });
}
bool inRange(const std::vector<ArrBlock>& b, int i) { return i >= 0 && i < int(b.size()); }
}  // namespace

bool Track::clipHasTrigs(int k) const {
    if (!clipFilled(k)) return false;
    const Clip& c = *clips[size_t(k)];
    const PageRange r = c.range();
    for (int p = r.lower; p <= r.upper && p < int(c.pages.size()); ++p)
        for (const auto& st : c.pages[size_t(p)])
            if (st.on) return true;
    return false;
}

int Track::clipSteps(int k) const {
    if (!clipFilled(k)) return ArrGrid::stepsPerBar;
    return std::max(1, clips[size_t(k)]->passTicks());
}

int Track::placeClip(const ArrBlock& b, bool skipEmpty) {
    const int pass = std::max(1, b.pattern ? b.pattern->passTicks() : clipSteps(b.clip));
    const int chunk = std::min(pass, maxClipBlock);
    const int len = std::max(1, b.length);
    if (len <= chunk && (b.offset % pass) + len <= pass) return !skipEmpty || clipHasTrig(b) ? placeBlock(b) : -1;
    int pos = 0, firstStart = -1;
    while (pos < len) {
        const int o = (b.offset + pos) % pass;
        const int seg = std::min({len - pos, chunk - o % chunk, pass - o});
        const ArrBlock piece(b.clip, b.start + pos, seg, o, b.pattern);
        // Участок без тригов — пусто, блока нет.
        if (!skipEmpty || clipHasTrig(piece)) {
            placeBlock(piece);
            if (firstStart < 0) firstStart = piece.start;
        }
        pos += seg;
    }
    for (size_t i = 0; i < blocks.size(); ++i)
        if (blocks[i].start == firstStart) return int(i);
    return -1;
}

bool Track::clipHasTrig(const ArrBlock& b) const {
    const Clip* c = b.pattern ? &*b.pattern : (clipFilled(b.clip) ? &*clips[size_t(b.clip)] : nullptr);
    if (!c) return false;
    const int n = std::min(b.length, std::max(1, c->passTicks()));
    for (int i = 0; i < n; ++i) {
        const int k = c->playIndex(c->localStep(b.offset + i));
        if (const Step* st = c->step(k); st && st->on) return true;
    }
    return false;
}

int Track::addBlock(int k, int at, std::optional<int> length) {
    return placeClip(ArrBlock(k, at, length.value_or(clipSteps(k))), false);
}

int Track::moveBlock(int i, int start) {
    if (!inRange(blocks, i)) return i;
    ArrBlock b = blocks[size_t(i)];
    blocks.erase(blocks.begin() + i);
    b.start = std::max(0, start);
    return placeBlock(b);
}

int Track::resizeBlock(int i, int start, int end) {
    if (!inRange(blocks, i)) return i;
    ArrBlock b = blocks[size_t(i)];
    blocks.erase(blocks.begin() + i);
    const int s = std::max(0, std::min(start, end - 1));
    b.offset += s - b.start;
    b.shiftAutos(s - b.start);
    b.start = s;
    b.length = std::max(1, end - s);
    return placeBlock(b);
}

void Track::deleteBlock(int i) {
    if (inRange(blocks, i)) blocks.erase(blocks.begin() + i);
}

void Track::deleteBlockClosing(int i) {
    if (!inRange(blocks, i)) return;
    const ArrBlock b = blocks[size_t(i)];
    blocks.erase(blocks.begin() + i);
    for (auto& x : blocks)
        if (x.start >= b.end()) x.start -= b.length;
    sortBlocks(blocks);
}

int Track::duplicateBlock(int i) {
    if (!inRange(blocks, i)) return i;
    ArrBlock b = blocks[size_t(i)];
    b.start = b.end();
    return placeBlock(b);
}

int Track::pasteBlock(int after, const ArrBlock& b) {
    if (!inRange(blocks, after)) return after;
    ArrBlock n = b;
    n.start = blocks[size_t(after)].end();
    return placeBlock(n);
}

void Track::shiftArrange(int x, int d) {
    if (d == 0) return;
    splitBlock(x);
    for (auto& b : blocks)
        if (b.start >= x) b.start += d;
    sortBlocks(blocks);
}

int Track::insertBlock(int k, int at, std::optional<int> length) {
    const int x = std::max(0, at);
    const int len = length.value_or(clipSteps(k));
    shiftArrange(x, len);
    return placeClip(ArrBlock(k, x, len), false);
}

int Track::resizeInserted(int i, int length) {
    if (!inRange(blocks, i)) return i;
    const ArrBlock b = blocks[size_t(i)];
    const int n = std::max(1, length);
    const int d = n - b.length;
    if (d == 0) return i;
    if (d > 0) {
        shiftArrange(b.end(), d);
    } else {
        clearArrange(b.start + n, b.end());
        for (auto& x : blocks)
            if (x.start >= b.end()) x.start += d;
        sortBlocks(blocks);
    }
    ArrBlock m = b;
    m.length = n;
    for (size_t j = 0; j < blocks.size(); ++j)
        if (blocks[j].start == b.start && blocks[j].clip == b.clip) {
            blocks[j] = m;
            return int(j);
        }
    return placeBlock(m);
}

void Track::duplicateBlocks() {
    if (blocks.empty()) return;
    int last = blocks[0].end();
    for (const auto& b : blocks) last = std::max(last, b.end());
    const int d = last - blocks[0].start;
    std::vector<ArrBlock> copy = blocks;
    for (auto& c : copy) c.start += d;
    blocks.insert(blocks.end(), copy.begin(), copy.end());
    sortBlocks(blocks);
}

void Track::remapBlocks(const std::function<std::optional<int>(int)>& map) {
    std::vector<ArrBlock> out;
    for (auto b : blocks) {
        const auto k = map(b.clip);
        if (!k) continue;
        b.clip = *k;
        out.push_back(b);
    }
    blocks = out;
}

int Track::arrangeEnd() const {
    int e = 0;
    for (const auto& b : blocks) e = std::max(e, b.end());
    return e;
}

void Track::splitArrange(int x) {
    splitBlock(x);
    sortBlocks(blocks);
}

void Track::splitBlock(int x) {
    for (auto& blk : blocks) {
        if (!(blk.start < x && blk.end() > x)) continue;
        const ArrBlock b = blk;
        const int cut = x - b.start;
        blk.length = cut;
        blk.pinCut(cut);
        ArrBlock right = blk;
        right.start = x;
        right.length = b.end() - x;
        right.offset = b.offset + cut;
        right.shiftAutos(cut);
        blocks.push_back(right);
        return;
    }
}

// MARK: — правки аранжировки проекта

int Project::arrangeEnd() const {
    int e = 0;
    for (const auto& t : tracks) e = std::max(e, t.arrangeEnd());
    return e;
}

void Project::pasteBlocks(int i, const std::vector<ArrBlock>& list) {
    if (i < 0 || i >= int(tracks.size())) return;
    tracks[size_t(i)].blocks.clear();
    for (const auto& b : list) tracks[size_t(i)].placeBlock(b);
}

std::vector<std::pair<int, ArrBlock>> Project::groupBlocks(const std::vector<BlockRef>& sel) const {
    std::vector<std::pair<int, ArrBlock>> out;
    for (const auto& s : sel)
        if (s.first >= 0 && s.first < int(tracks.size()) && inRange(tracks[size_t(s.first)].blocks, s.second))
            out.push_back({s.first, tracks[size_t(s.first)].blocks[size_t(s.second)]});
    return out;
}

void Project::removeGroup(const std::vector<BlockRef>& sel) {
    auto sorted = sel;
    std::stable_sort(sorted.begin(), sorted.end(), [](const BlockRef& a, const BlockRef& b) {
        return a.first == b.first ? a.second > b.second : a.first < b.first;
    });
    for (const auto& s : sorted)
        if (s.first >= 0 && s.first < int(tracks.size())) tracks[size_t(s.first)].deleteBlock(s.second);
}

std::vector<Project::BlockRef> Project::blockIndices(const std::vector<std::pair<int, ArrBlock>>& list) const {
    std::vector<BlockRef> out;
    for (const auto& g : list) {
        const auto& bl = tracks[size_t(g.first)].blocks;
        for (size_t i = 0; i < bl.size(); ++i)
            if (bl[i] == g.second) {
                out.push_back({g.first, int(i)});
                break;
            }
    }
    return out;
}

void Project::deleteBlocks(const std::vector<BlockRef>& sel, bool closing) {
    if (!closing) {
        removeGroup(sel);
        return;
    }
    auto sorted = sel;
    std::stable_sort(sorted.begin(), sorted.end(), [](const BlockRef& a, const BlockRef& b) {
        return a.first == b.first ? a.second > b.second : a.first < b.first;
    });
    for (const auto& s : sorted)
        if (s.first >= 0 && s.first < int(tracks.size())) tracks[size_t(s.first)].deleteBlockClosing(s.second);
}

std::vector<Project::BlockRef> Project::moveBlocks(const std::vector<BlockRef>& sel, int d0) {
    const auto list = groupBlocks(sel);
    if (list.empty()) return {};
    int first = list[0].second.start;
    for (const auto& g : list) first = std::min(first, g.second.start);
    const int d = std::max(d0, -first);
    removeGroup(sel);
    std::vector<std::pair<int, ArrBlock>> placed;
    for (const auto& g : list) {
        ArrBlock b = g.second;
        b.start += d;
        tracks[size_t(g.first)].placeBlock(b);
        placed.push_back({g.first, b});
    }
    return blockIndices(placed);
}

std::vector<Project::BlockRef> Project::duplicateBlocks(const std::vector<BlockRef>& sel) {
    const auto list = groupBlocks(sel);
    if (list.empty()) return {};
    int a = list[0].second.start, e = list[0].second.end();
    for (const auto& g : list) {
        a = std::min(a, g.second.start);
        e = std::max(e, g.second.end());
    }
    std::vector<std::pair<int, ArrBlock>> placed;
    for (const auto& g : list) {
        ArrBlock b = g.second;
        b.start += e - a;
        tracks[size_t(g.first)].placeBlock(b);
        placed.push_back({g.first, b});
    }
    return blockIndices(placed);
}

void Project::splitArrange(int t, int a, int b) {
    if (t < 0 || t >= int(tracks.size())) return;
    tracks[size_t(t)].splitArrange(a);
    tracks[size_t(t)].splitArrange(b);
}

void Project::setTrackColor(int i, int color) {
    if (i < 0 || i >= int(tracks.size()) || color < 0 || color >= ArrPalette::count) return;
    tracks[size_t(i)].color = color;
}

namespace {
void sortMarkers(std::vector<ArrMarker>& m) {
    std::stable_sort(m.begin(), m.end(), [](const ArrMarker& a, const ArrMarker& b) { return a.at < b.at; });
}
int indexOf(const std::vector<ArrMarker>& list, const ArrMarker& m) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i] == m) return int(i);
    return 0;
}
}  // namespace

int Project::addMarker(int at, const std::string& name, std::optional<int> end) {
    const int x = std::max(0, at);
    auto& ms = arrange.markers;
    ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const ArrMarker& m) { return m.at == x; }), ms.end());
    std::optional<int> e = end;
    if (e && *e <= x) e.reset();
    ArrMarker m;
    m.at = x;
    m.name = markerName(name);
    m.end = e;
    ms.push_back(m);
    sortMarkers(ms);
    return indexOf(ms, m);
}

int Project::moveMarker(int k, int at) {
    auto& ms = arrange.markers;
    if (k < 0 || k >= int(ms.size())) return k;
    ArrMarker m = ms[size_t(k)];
    ms.erase(ms.begin() + k);
    const int x = std::max(0, at);
    // Секция с концом переезжает целиком.
    if (m.end) m.end = *m.end + (x - m.at);
    m.at = x;
    ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const ArrMarker& o) { return o.at == m.at; }), ms.end());
    ms.push_back(m);
    sortMarkers(ms);
    return indexOf(ms, m);
}

void Project::setMarkerStart(int k, int x) {
    const auto list = arrange.markers;
    if (k < 0 || k >= int(list.size())) return;
    int lo = 0;
    if (k > 0) lo = std::max(list[size_t(k - 1)].at + 1, sectionEnd(k - 1).value_or(0));
    int hi = INT_MAX;
    if (auto e = sectionEnd(k)) hi = *e - 1;
    arrange.markers[size_t(k)].at = std::min(hi, std::max(lo, x));
}

void Project::setMarkerEnd(int k, int x) {
    const auto list = arrange.markers;
    if (k < 0 || k >= int(list.size())) return;
    int e = std::max(list[size_t(k)].at + 1, x);
    if (k + 1 < int(list.size())) e = std::min(e, list[size_t(k + 1)].at);
    arrange.markers[size_t(k)].end = e;
}

void Project::renameMarker(int k, const std::string& name) {
    if (k >= 0 && k < int(arrange.markers.size())) arrange.markers[size_t(k)].name = markerName(name);
}

void Project::setMarkerColor(int k, int color) {
    if (k < 0 || k >= int(arrange.markers.size()) || color < 0 || color >= ArrPalette::count) return;
    arrange.markers[size_t(k)].color = color;
}

void Project::deleteMarker(int k) {
    if (k >= 0 && k < int(arrange.markers.size())) arrange.markers.erase(arrange.markers.begin() + k);
}

std::optional<int> Project::section(int x) const {
    const auto& ms = arrange.markers;
    int k = -1;
    for (int i = int(ms.size()) - 1; i >= 0; --i)
        if (ms[size_t(i)].at <= x) {
            k = i;
            break;
        }
    if (k < 0) return std::nullopt;
    if (auto e = sectionEnd(k); e && x >= *e) return std::nullopt;
    return ms[size_t(k)].at <= x ? std::optional<int>(k) : std::nullopt;
}

std::optional<int> Project::sectionEnd(int k) const {
    const auto& list = arrange.markers;
    if (k < 0 || k >= int(list.size())) return std::nullopt;
    const std::optional<int> next = k + 1 < int(list.size()) ? std::optional<int>(list[size_t(k + 1)].at) : std::nullopt;
    if (list[size_t(k)].end) return next ? std::min(*list[size_t(k)].end, *next) : *list[size_t(k)].end;
    return next;
}

void Project::setLoop(int a, int b) {
    arrange.loopFrom = std::max(0, std::min(a, b));
    arrange.loopTo = std::max(0, std::max(a, b));
    if (!arrange.hasLoop()) arrange.loop = false;
}

std::optional<int> Project::rowToArrange(int k, std::optional<int> at, bool repeats) {
    if (k < 0 || k >= int(scenes.size())) return std::nullopt;
    const int reps = repeats ? std::max(1, scenes[size_t(k)].repeats) : 1;
    std::vector<int> all;
    for (int t = 0; t < int(tracks.size()); ++t) all.push_back(t);
    const auto span = placeRow(k, all, reps, at);
    if (!span) return std::nullopt;
    if (!scenes[size_t(k)].name.empty()) {
        auto& ms = arrange.markers;
        ms.erase(std::remove_if(ms.begin(), ms.end(),
                                [&](const ArrMarker& m) { return m.at >= span->first && m.at < span->second; }),
                 ms.end());
        addMarker(span->first, scenes[size_t(k)].name, span->second);
    }
    return span->second;
}

std::optional<std::pair<int, int>> Project::placeRow(int k, const std::vector<int>& only, int reps, std::optional<int> at) {
    if (k < 0 || k >= int(scenes.size())) return std::nullopt;
    std::vector<int> list;
    for (int t : only)
        if (t >= 0 && t < int(tracks.size()) && tracks[size_t(t)].clipHasTrigs(k)) list.push_back(t);
    int longest = 0;
    for (int t : list) longest = std::max(longest, tracks[size_t(t)].clipSteps(k));
    if (longest == 0) return std::nullopt;
    const int start = std::max(0, at.value_or(arrangeEnd()));
    const int end = start + longest * std::max(1, reps);
    for (int t : list) tracks[size_t(t)].placeClip(ArrBlock(k, start, end - start));
    return std::make_pair(start, end);
}

std::string Project::markerName(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    // 12 знаков (символов Юникода, как prefix(12) в Swift).
    std::string out;
    int count = 0;
    for (size_t i = a; i < b && count < 12;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        out.append(s, i, len);
        i += len;
        ++count;
    }
    return out;
}

}  // namespace jambo
