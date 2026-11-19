// Порт Core/Model/TrackEdits.swift (ветка swift JAMbo): правки дорожки — страницы, p-lock, триги, MORPH; дорожки
// проекта. Project::remapEqDyn — из Core/EQ/EqModel.swift.
#include <algorithm>
#include <cstdlib>
#include "Project.h"

namespace jambo {

namespace {
const int N = Params::stepsPerPage;
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
}  // namespace

void Track::changePages(const std::function<void(std::vector<Page>&)>& change) {
    const bool full = length() == int(pages().size()) * N;
    const bool allPlay = playFrom() == 0 && playTo() == int(pages().size()) - 1;
    Clip& c = editOpen();
    change(c.pages);
    const int last = int(c.pages.size()) - 1;
    const int maxLen = int(c.pages.size()) * N;
    c.length = full ? maxLen : std::min(c.length, maxLen);
    if (allPlay) {
        c.playFrom = 0;
        c.playTo = last;
    }
    c.playTo = std::min(c.playTo, last);
    c.playFrom = std::min(c.playFrom, c.playTo);
}

PageRange Track::pageRange(int i) const {
    if (i >= playFrom() && i <= playTo()) return {playFrom(), playTo()};
    return {i, i};
}

void Track::setPlayPages(int a, int b) {
    Clip& c = editOpen();
    c.playFrom = std::max(0, std::min(a, b));
    c.playTo = std::min(int(c.pages.size()) - 1, std::max(a, b));
}

bool Track::endsShortOnLastPage() const {
    const int n = int(pages().size());
    return length() < n * N && (length() + N - 1) / N == n;
}

void Track::addPage() {
    if (int(pages().size()) >= Params::maxPages) return;
    if (endsShortOnLastPage()) {
        appendSteps(std::vector<Step>(size_t(N)));
        return;
    }
    changePages([](std::vector<Page>& pg) { pg.push_back(emptyPage()); });
}

bool Track::appendSteps(const std::vector<Step>& steps) {
    const int from = length();
    const int newLen = from + int(steps.size());
    if (newLen > Params::maxPages * N) return false;
    const bool allPlay = playFrom() == 0 && playTo() == int(pages().size()) - 1;
    std::vector<Page> pg = pages();
    while (int(pg.size()) * N < newLen) pg.push_back(emptyPage());
    for (int k = 0; k < int(steps.size()); ++k) {
        const int at = from + k;
        pg[size_t(at / N)][size_t(at % N)] = steps[size_t(k)];
    }
    // Скрытые шаги за новой длиной на последней странице — пустые.
    for (int at = newLen; at < int(pg.size()) * N; ++at) pg[size_t(at / N)][size_t(at % N)] = Step();
    Clip& c = editOpen();
    c.pages = std::move(pg);
    c.length = newLen;
    if (allPlay) {
        c.playFrom = 0;
        c.playTo = int(c.pages.size()) - 1;
    }
    return true;
}

bool Track::duplicatePages(int i) {
    const PageRange r = pageRange(i);
    const int count = r.upper - r.lower + 1;
    if (endsShortOnLastPage() && r.upper == int(pages().size()) - 1) {
        const int start = r.lower * N;
        std::vector<Step> copy;
        for (int k = start; k < length(); ++k) copy.push_back(pages()[size_t(k / N)][size_t(k % N)]);
        const bool playing = playTo() == r.upper && playFrom() == r.lower;
        if (!appendSteps(copy)) return false;
        // Играл этот диапазон — играет и копия.
        if (playing) setPlayTo(int(pages().size()) - 1);
        return true;
    }
    if (int(pages().size()) + count > Params::maxPages) return false;
    const std::vector<Page> copy(pages().begin() + r.lower, pages().begin() + r.upper + 1);
    changePages([&](std::vector<Page>& pg) { pg.insert(pg.begin() + r.upper + 1, copy.begin(), copy.end()); });
    if (playFrom() > r.upper) {
        setPlayFrom(playFrom() + count);
        setPlayTo(playTo() + count);
    }
    return true;
}

std::vector<Page> Track::copyPages(int i) const {
    const PageRange r = pageRange(i);
    return std::vector<Page>(pages().begin() + r.lower, pages().begin() + r.upper + 1);
}

void Track::pastePages(int i, const std::vector<Page>& clip) {
    const int a = pageRange(i).lower;
    changePages([&](std::vector<Page>& pg) {
        for (int k = 0; k < int(clip.size()); ++k) {
            if (a + k < int(pg.size())) {
                pg[size_t(a + k)] = clip[size_t(k)];
            } else if (int(pg.size()) < Params::maxPages) {
                pg.push_back(clip[size_t(k)]);
            }
        }
    });
}

void Track::clearPages(int i) {
    const PageRange r = pageRange(i);
    std::vector<Page>& pg = pagesMut();
    for (int k = r.lower; k <= r.upper; ++k) pg[size_t(k)] = emptyPage();
}

bool Track::deletePages(int i, bool single) {
    const PageRange r = single ? PageRange{i, i} : pageRange(i);
    const int count = r.upper - r.lower + 1;
    if (r.lower < 0 || r.upper >= int(pages().size()) || int(pages().size()) - count < 1) return false;
    const int from = playFrom();
    const int to = playTo();
    changePages([&](std::vector<Page>& pg) { pg.erase(pg.begin() + r.lower, pg.begin() + r.upper + 1); });
    const int n = int(pages().size());
    if (from >= r.lower && to <= r.upper) {
        setPlayFrom(std::min(r.lower, n - 1));
        setPlayTo(playFrom());
    } else if (from > r.upper) {
        setPlayFrom(from - count);
        setPlayTo(to - count);
    } else if (from <= r.lower && r.upper <= to) {
        setPlayTo(to - count);
    }
    setPlayTo(std::max(playFrom(), std::min(playTo(), n - 1)));
    return true;
}

bool Track::nudgePages(int i, Nudge dir) {
    const PageRange r = pageRange(i);
    std::vector<Step> steps;
    for (int p = r.lower; p <= r.upper; ++p) steps.insert(steps.end(), pages()[size_t(p)].begin(), pages()[size_t(p)].end());
    switch (dir) {
        case Nudge::left:
        case Nudge::right: {
            if (dir == Nudge::left) {
                std::rotate(steps.begin(), steps.begin() + 1, steps.end());
            } else {
                std::rotate(steps.begin(), steps.end() - 1, steps.end());
            }
            std::vector<Page>& pg = pagesMut();
            for (int k = 0; k <= r.upper - r.lower; ++k)
                pg[size_t(r.lower + k)] = Page(steps.begin() + k * N, steps.begin() + (k + 1) * N);
            return true;
        }
        case Nudge::up:
        case Nudge::down: {
            const int d = dir == Nudge::up ? 12 : -12;
            bool any = false;
            for (const Step& s : steps)
                for (int n : s.notes) {
                    any = true;
                    if (n + d < 0 || n + d > 127) return false;
                }
            if (!any) return false;
            std::vector<Page>& pg = pagesMut();
            for (int p = r.lower; p <= r.upper; ++p)
                for (Step& s : pg[size_t(p)])
                    for (int& n : s.notes) n += d;
            return true;
        }
    }
    return false;
}

bool Track::transposePages(int i, int d, int scale, int root) {
    const PageRange r = pageRange(i);
    std::vector<int> notes;
    for (int p = r.lower; p <= r.upper; ++p)
        for (const Step& s : pages()[size_t(p)]) notes.insert(notes.end(), s.notes.begin(), s.notes.end());
    if (notes.empty() || d == 0) return false;
    const std::vector<int>& steps = Scales::all[size_t(Scales::wrap(scale, int(Scales::all.size())))].steps;
    auto inScale = [&](int n) {
        return std::find(steps.begin(), steps.end(), Scales::wrap(n - root, 12)) != steps.end();
    };
    auto move = [&](int n) {
        int m = n;
        do { m += d > 0 ? 1 : -1; } while (!inScale(m) && std::abs(m - n) < 12);
        return m;
    };
    for (int n : notes)
        if (move(n) < 0 || move(n) > 127) return false;
    std::vector<Page>& pg = pagesMut();
    for (int p = r.lower; p <= r.upper; ++p)
        for (Step& s : pg[size_t(p)])
            for (int& n : s.notes) n = move(n);
    return true;
}

// MARK: — шаги и p-lock

void Track::withStep(int index, const std::function<void(Step&)>& change) {
    const int p = index / N;
    if (p < 0 || p >= int(pages().size())) return;
    change(pagesMut()[size_t(p)][size_t(index % N)]);
}

void Track::toggleStep(int index, const std::vector<int>& notes) {
    withStep(index, [&](Step& s) {
        if (s.on) {
            s = Step();
        } else {
            s.on = true;
            if (s.notes.empty()) s.notes = notes;
        }
    });
}

void Track::setLock(int index, const std::string& id, double value) {
    withStep(index, [&](Step& s) {
        s.locks[id] = value;
        if (s.soundLocks) {
            std::vector<std::string> sl;
            for (const auto& k : *s.soundLocks) if (k != id) sl.push_back(k);
            s.soundLocks = sl;
        }
        // P-lock Level шага — velocity всех его нот.
        if (id == "level") s.clearVels();
    });
}

void Track::clearLock(int index, const std::string& id) {
    withStep(index, [&](Step& s) {
        s.locks.erase(id);
        if (id == "level") s.clearVels();
    });
}

void Track::clearAllLocks() {
    for (Page& pg : pagesMut())
        for (Step& s : pg) {
            s.locks.clear();
            s.clearVels();
        }
}

bool Track::isTail(int index) const {
    for (int k = index - 1; k >= 0; --k) {
        const Step* s = step(k);
        if (s && s->on) return s->len > index - k;
    }
    return false;
}

bool Track::hasManualLocks(const Step& s) {
    if (s.hasVel()) return true;
    const std::vector<std::string> sound = s.soundLocks.value_or(std::vector<std::string>());
    for (const auto& kv : s.locks)
        if (!contains(sound, kv.first)) return true;
    return false;
}

const std::set<std::string>& Track::col4Ids() {
    static const std::set<std::string> ids = [] {
        std::set<std::string> v;
        for (const auto* cols : {&Params::trackColumns(), &Params::fxColumns(), &Params::modColumns()})
            for (const auto& p : (*cols)[3]) v.insert(p.id);
        return v;
    }();
    return ids;
}

Track::LockKinds Track::manualLockKinds(const Step& s) {
    const std::vector<std::string> sound = s.soundLocks.value_or(std::vector<std::string>());
    LockKinds k{false, s.hasVel()};
    for (const auto& kv : s.locks) {
        if (contains(sound, kv.first)) continue;
        if (col4Ids().count(kv.first)) k.col4 = true; else k.other = true;
    }
    return k;
}

void Track::remapMixLocks(const std::function<int(int)>& map) {
    for (auto& clip : clips) {
        if (!clip) continue;
        for (Page& pg : clip->pages)
            for (Step& s : pg) {
                if (s.locks.empty()) continue;
                ParamMap out;
                for (const auto& [id, v] : s.locks) {
                    const int k = Params::mixTrack(id);
                    if (k >= 0) {
                        const int m = map(k);
                        if (m >= 0) out[Params::mixGrid()[size_t(m)].id] = v;
                    } else {
                        out[id] = v;
                    }
                }
                s.locks = std::move(out);
            }
    }
}

void Track::movePage(int from, int to) {
    const int n = int(pages().size());
    if (from == to || from < 0 || to < 0 || from >= n || to >= n) return;
    const bool single = playFrom() == from && playTo() == from;
    std::vector<Page>& pg = pagesMut();
    Page page = std::move(pg[size_t(from)]);
    pg.erase(pg.begin() + from);
    pg.insert(pg.begin() + to, std::move(page));
    if (single) {
        setPlayFrom(to);
        setPlayTo(to);
    }
}

// MARK: — MORPH

void Track::morphParam(const std::string& id) {
    morphEdited = true;
    if (!morphOrig.count(id)) {
        auto it = params.find(id);
        const ParamDef* d = Params::byId(id);
        morphOrig[id] = it != params.end() ? it->second : (d ? d->def : 0);
    }
}

void Track::morphAssign(const std::string& id) {
    morphEdited = true;
    if (!morphOrigAssign.count(id)) {
        auto it = assigns.find(id);
        morphOrigAssign[id] = it != assigns.end() ? std::optional<ModAssign>(it->second) : std::nullopt;
    }
}

void Track::undoMorph() {
    for (const auto& [id, v] : morphOrig)
        if (!Project::morphFxId(id)) params[id] = v;
    for (const auto& [id, a] : morphOrigAssign) {
        if (a) assigns[id] = *a; else assigns.erase(id);
    }
    morphOrig.clear();
    morphOrigAssign.clear();
    morphEdited = false;
}

// MARK: — дорожки проекта

void Project::addTrack() {
    if (!canAddTrack()) return;
    Track t(int(tracks.size()));
    t.stopped = true;
    // Стиль случайного ритма — общий для проекта.
    t.drumStyle = tracks.empty() ? 0 : tracks.front().drumStyle;
    tracks.push_back(std::move(t));
    fillRows();
}

void Project::deleteTrack(int i) {
    if (!canDeleteTrack(i)) return;
    tracks.erase(tracks.begin() + i);
    // P-lock громкостей на шагах MIX: удалённой снимаются, правые сдвигаются.
    auto map = [i](int k) { return k == i ? -1 : (k > i ? k - 1 : k); };
    chanSeqs[mixSeq].remapMixLocks(map);
    remapEqDyn(map);
}

void Project::moveTrack(int from, int to) {
    const int n = int(tracks.size());
    if (from == to || from < 0 || to < 0 || from >= n || to >= n) return;
    Track t = std::move(tracks[size_t(from)]);
    tracks.erase(tracks.begin() + from);
    tracks.insert(tracks.begin() + to, std::move(t));
    auto map = [from, to](int k) {
        if (k == from) return to;
        if (from < to && k > from && k <= to) return k - 1;
        if (to < from && k >= to && k < from) return k + 1;
        return k;
    };
    chanSeqs[mixSeq].remapMixLocks(map);
    remapEqDyn(map);
}

void Project::pasteTrack(int i, const Track& c) {
    if (i < 0 || i >= int(tracks.size())) return;
    const Track& old = tracks[size_t(i)];
    // Переход MIX: свой звук скопированной — home, если она играла клип PREV.
    const TrackSound own = c.home ? *c.home : c.sound();
    Track t = c;
    t.clips = old.clips;
    t.slot = old.slot;
    t.open = old.open;
    t.stopped = old.stopped;
    t.mute = old.mute;
    t.solo = old.solo;
    t.id = old.id;
    t.name = old.name;
    t.blocks = old.blocks;
    t.color = old.color;
    if (old.home) {
        // Дорожка играет клип PREV: звучит старая дорожка, вставленное станет её звуком после PREV.
        t.setSound(old.sound());
        t.home = own;
    } else {
        t.setSound(own);
        t.home.reset();
    }
    tracks[size_t(i)] = std::move(t);
}

void Project::renameTrack(int i, const std::string& name) {
    // trimmingCharacters(in: .whitespaces) и prefix(12) — по символам (UTF-8), как в Swift.
    size_t a = 0, b = name.size();
    while (a < b && (name[a] == ' ' || name[a] == '\t')) ++a;
    while (b > a && (name[b - 1] == ' ' || name[b - 1] == '\t')) --b;
    std::string n;
    int chars = 0;
    for (size_t k = a; k < b; ++k) {
        const unsigned char ch = static_cast<unsigned char>(name[k]);
        if ((ch & 0xC0) != 0x80) {
            if (chars == 12) break;
            ++chars;
        }
        n.push_back(name[k]);
    }
    if (!n.empty() && i >= 0 && i < int(tracks.size())) tracks[size_t(i)].name = n;
}

void Project::remapEqDyn(const std::function<int(int)>& map) {
    auto fix = [&](Eq& e) {
        for (auto& b : e.bands) {
            if (!b || b->dyn < 1) continue;
            const int m = map(b->dyn - 1);
            b->dyn = m >= 0 ? m + 1 : -1;
        }
    };
    for (auto& t : tracks) fix(t.eq);
    fix(masterEq);
}

}  // namespace jambo
