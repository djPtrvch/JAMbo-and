// Порт Core/Model/Clips.swift (ветка swift JAMbo): клипы и строки сетки паттернов.
#include "Project.h"
#include <algorithm>

namespace jambo {

void Track::activate(int k) {
    if (open == slot) open = k;
    moveSlot(k);
}

void Track::moveSlot(int k) {
    if (home) {
        const TrackSound own = *home;
        if (clipFilled(slot) && clips[size_t(slot)]->src) {
            TrackSound cur = sound();
            cur.vol = clips[size_t(slot)]->src->vol;
            clips[size_t(slot)]->src = cur;
        }
        setSound(own);
        home.reset();
    }
    slot = k;
    settleSound();
}

void Track::putEmptyClip(int k, std::optional<int> scale, std::optional<int> root) {
    Clip c;
    c.scale = scale;
    c.root = root;
    clips[size_t(k)] = c;
}

std::optional<Clip> Track::clipCopy(int k) const {
    if (!clipFilled(k)) return std::nullopt;
    Clip c = *clips[size_t(k)];
    if (k == slot && home && c.src) {
        TrackSound cur = sound();
        cur.vol = c.src->vol;
        c.src = cur;
    }
    return c;
}

std::optional<PageRange> Track::clipRange(int k) const {
    if (!clipFilled(k)) return std::nullopt;
    return clips[size_t(k)]->range();
}

void Track::setClip(int k, const std::optional<Clip>& c) {
    if (k != slot) {
        clips[size_t(k)] = c;
        return;
    }
    // Активная ячейка: прежний свой звук клипа уходит, звук — нового клипа (или дорожки).
    if (home) {
        setSound(*home);
        home.reset();
    }
    clips[size_t(k)] = c;
    settleSound();
}

void Track::clearClip(int k) {
    if (!clipFilled(k)) return;
    Clip& c = *clips[size_t(k)];
    c.pages = {emptyPage()};
    c.length = Params::stepsPerPage;
    c.playFrom = 0;
    c.playTo = 0;
    c.rate.reset();
}

bool Project::rowFilled(int k) const {
    for (const auto& t : tracks) if (t.clipFilled(k)) return true;
    return false;
}

int Project::visibleRows() const {
    const int lim = rowLimit();
    int n = std::max(1, rows);
    for (int k = 0; k < lim; ++k) if (rowFilled(k)) n = std::max(n, k + 1);
    return std::min(n, lim);
}

void Project::fillRows() {
    const int n = visibleRows();
    for (auto& t : tracks)
        for (int k = 0; k < n; ++k)
            if (!t.clipFilled(k)) t.putEmptyClip(k, scale, root);
}

bool Project::audible(int i) const {
    if (i < 0 || i >= int(tracks.size())) return false;
    for (const auto& t : tracks) if (t.solo) return tracks[size_t(i)].solo;
    return !tracks[size_t(i)].mute;
}

void Project::addRow() {
    if (!canAddRow()) return;
    const int r = visibleRows();
    const int prevIndex = Params::patternSlots - 1;
    for (int i = 0; i < int(tracks.size()); ++i) {
        const Track& t = tracks[size_t(i)];
        if (t.clipFilled(r)) continue;
        if (plusCopy && !t.stopped && !t.blank() && audible(i)) {
            auto c = t.clipCopy(t.slot);
            // Копия клипа PREV — только паттерн (звук старой дорожки остаётся в PREV).
            if (c && prevRow && t.slot == prevIndex) c->src.reset();
            tracks[size_t(i)].clips[size_t(r)] = c;
        } else {
            tracks[size_t(i)].putEmptyClip(r, scale, root);
        }
    }
    rows = r + 1;
}

void Project::duplicateRow(int k) {
    if (!canAddRow()) return;
    const int n = visibleRows();
    const int lim = rowLimit();
    for (auto& t : tracks) {
        auto c = t.clips;
        c.insert(c.begin() + k + 1, t.clipCopy(k));
        c.erase(c.begin() + lim);
        t.clips = c;
        if (t.slot > k && t.slot < lim) t.slot += 1;
        if (t.open > k && t.open < lim) t.open += 1;
        t.remapBlocks([&](int b) -> std::optional<int> { return b > k && b < lim ? b + 1 : b; });
    }
    scenes.insert(scenes.begin() + k + 1, scenes[size_t(k)]);
    scenes.erase(scenes.begin() + lim);
    rows = n + 1;
}

void Project::deleteRow(int k) {
    const int n = visibleRows();
    const int lim = rowLimit();
    for (auto& t : tracks) {
        auto c = t.clips;
        c.erase(c.begin() + k);
        c.insert(c.begin() + (lim - 1), std::nullopt);
        const int slot = t.slot;
        int first = -1;
        for (int j = 0; j < lim; ++j)
            if (c[size_t(j)]) {
                first = j;
                break;
            }
        const int fallback = c[size_t(k)] ? k : (first >= 0 ? first : std::min(k, std::max(0, n - 2)));
        int active = slot > k && slot < lim ? slot - 1 : slot;
        if (slot == k) {
            active = fallback;
            t.stopped = true;
            // Свой звук удалённого клипа уходит вместе с ним.
            if (t.home) {
                t.setSound(*t.home);
                t.home.reset();
            }
        }
        const int open = t.open;
        t.clips = c;
        t.slot = active;
        t.settleSound();
        t.open = open == k ? fallback : (open > k && open < lim ? open - 1 : open);
        // Блоки удалённых клипов уходят, ниже — за своими строками.
        t.remapBlocks([&](int b) -> std::optional<int> {
            if (b == k) return std::nullopt;
            return b > k && b < lim ? b - 1 : b;
        });
    }
    scenes.erase(scenes.begin() + k);
    scenes.insert(scenes.begin() + (lim - 1), SceneRow());
    rows = std::max(1, n - 1);
    if (arrStart > k || arrStart >= rows) arrStart = std::max(0, arrStart - 1);
}

void Project::moveRow(int a, int b) {
    const int n = visibleRows();
    if (a == b || a < 0 || b < 0 || a >= n || b >= n) return;
    auto moved = [&](int k) {
        if (k == a) return b;
        if (a < b && k > a && k <= b) return k - 1;
        if (b < a && k >= b && k < a) return k + 1;
        return k;
    };
    for (auto& t : tracks) {
        auto c = t.clips;
        auto clip = c[size_t(a)];
        c.erase(c.begin() + a);
        c.insert(c.begin() + b, clip);
        t.clips = c;
        t.slot = moved(t.slot);
        t.open = moved(t.open);
        t.remapBlocks([&](int x) -> std::optional<int> { return moved(x); });
    }
    const SceneRow scene = scenes[size_t(a)];
    scenes.erase(scenes.begin() + a);
    scenes.insert(scenes.begin() + b, scene);
    arrStart = moved(arrStart);
}

Project::RowClip Project::copyRow(int k) const {
    RowClip r;
    for (const auto& t : tracks) r.clips.push_back(t.clipCopy(k));
    r.scene = scenes[size_t(k)];
    return r;
}

void Project::pasteRow(int k, const RowClip& clip) {
    for (size_t i = 0; i < tracks.size() && i < clip.clips.size(); ++i) {
        if (!clip.clips[i]) continue;
        tracks[i].setClip(k, clip.clips[i]);
        // Клип заменён: его блоки удаляются.
        tracks[i].remapBlocks([&](int b) -> std::optional<int> {
            if (b == k) return std::nullopt;
            return b;
        });
    }
    fillRows();
    scenes[size_t(k)] = clip.scene;
}

void Project::setScene(int k, const std::string& name, int repeats) {
    // Без пробелов по краям, не длиннее 12 знаков.
    size_t a = 0, b = name.size();
    while (a < b && name[a] == ' ') ++a;
    while (b > a && name[b - 1] == ' ') --b;
    std::string n = name.substr(a, b - a);
    size_t i = 0;
    int cnt = 0;
    while (i < n.size() && cnt < 12) {
        const unsigned char c = (unsigned char)n[i];
        i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        ++cnt;
    }
    SceneRow s;
    s.name = n.substr(0, std::min(i, n.size()));
    s.repeats = std::max(1, std::min(64, repeats));
    scenes[size_t(k)] = s;
}

std::string Project::sceneName(int k) const {
    return scenes[size_t(k)].name.empty() ? std::to_string(k + 1) : scenes[size_t(k)].name;
}

}  // namespace jambo
