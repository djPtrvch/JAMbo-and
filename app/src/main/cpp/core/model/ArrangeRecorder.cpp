// Запись аранжировки из сетки (порт Core/Model/ArrangeRecorder.swift, ветка swift JAMbo).
#include "ArrangeRecorder.h"
#include "Project.h"
#include <algorithm>
#include <cmath>

namespace jambo {

namespace {
template <typename T>
void moveItem(std::vector<T>& v, int from, int to) {
    T x = std::move(v[size_t(from)]);
    v.erase(v.begin() + from);
    v.insert(v.begin() + to, std::move(x));
}
int moved(int t, int from, int to) {
    if (t == from) return to;
    if (from < to && t > from && t <= to) return t - 1;
    if (from > to && t >= to && t < from) return t + 1;
    return t;
}
}  // namespace

ArrRecorder::ArrRecorder(int tracks)
    : open_(size_t(tracks)), muted_(size_t(tracks), false), played_(size_t(tracks)), fired_(size_t(tracks)),
      heard_(size_t(tracks)) {}

std::vector<ArrRecorder::Seg> ArrRecorder::current() const {
    if (!active_) return {};
    std::vector<Seg> out = done_;
    for (const auto& s : open_) {
        if (!s) continue;
        Seg x = *s;
        x.end = std::max(x.start, pos_ + 1);
        if (x.end > x.start) out.push_back(x);
    }
    return out;
}

std::optional<int> ArrRecorder::openSlot(int t) const {
    if (!active_ || t < 0 || t >= int(open_.size()) || !open_[size_t(t)]) return std::nullopt;
    return open_[size_t(t)]->slot;
}

void ArrRecorder::notePlayed(int t, int p, const Step* step) {
    if (!active_ || p < 0 || t < 0 || t >= int(open_.size()) || !open_[size_t(t)]) return;
    played_[size_t(t)][pos_] = p;
    if (step) heard_[size_t(t)][pos_] = *step;
}

std::optional<int> ArrRecorder::liveNote(int t, int x, const std::vector<int>& notes, std::optional<double> velocity) {
    if (!active_ || notes.empty() || t < 0 || t >= int(open_.size()) || !open_[size_t(t)] || open_[size_t(t)]->silent)
        return std::nullopt;
    LiveNote n;
    n.track = t;
    n.at = std::max(open_[size_t(t)]->start, x);
    n.notes = notes;
    n.velocity = velocity;
    live_.push_back(n);
    return int(live_.size()) - 1;
}

void ArrRecorder::liveLength(int i, int len) {
    if (i >= 0 && i < int(live_.size())) live_[size_t(i)].len = std::max(1, len);
}

void ArrRecorder::noteFired(int t) {
    if (!active_ || t < 0 || t >= int(open_.size()) || !open_[size_t(t)]) return;
    fired_[size_t(t)].insert(pos_);
}

void ArrRecorder::begin(int at, const std::vector<State>& states, const std::vector<bool>& audible) {
    active_ = true;
    done_.clear();
    pos_ = at;
    for (size_t t = 0; t < played_.size(); ++t) {
        played_[t].clear();
        fired_[t].clear();
        heard_[t].clear();
    }
    live_.clear();
    for (size_t t = 0; t < open_.size(); ++t) {
        open_[t].reset();
        muted_[t] = t < audible.size() && !audible[t];
        if (t < states.size() && states[t])
            open_[t] = Seg(int(t), states[t]->first, at, at, states[t]->second, muted_[t]);
    }
}

void ArrRecorder::audible(const std::vector<bool>& flags, int at) {
    if (!active_) return;
    const int x = std::min(pos_ + 1, std::max(pos_, at));
    for (size_t t = 0; t < open_.size(); ++t) {
        if (!(t < flags.size() && muted_[t] == flags[t])) continue;
        muted_[t] = !flags[t];
        if (!open_[t]) continue;
        const Seg s = *open_[t];
        const int off = s.offset + std::max(0, x - s.start);
        close(int(t), x);
        open_[t] = Seg(int(t), s.slot, x, x, off, muted_[t]);
    }
}

void ArrRecorder::step(int p) {
    if (!active_ || p == pos_ || p == pos_ + 1) {
        pos_ = p;
        return;
    }
    const int end = pos_ + 1;
    for (size_t t = 0; t < open_.size(); ++t) {
        if (!open_[t]) continue;
        const Seg s = *open_[t];
        close(int(t), end);
        open_[t] = Seg(int(t), s.slot, p, p, s.offset + (end - s.start), s.silent);
    }
    pos_ = p;
}

void ArrRecorder::clip(int t, std::optional<int> slot) {
    if (!active_ || t < 0 || t >= int(open_.size())) return;
    close(t, pos_);
    open_[size_t(t)].reset();
    if (slot) open_[size_t(t)] = Seg(t, *slot, pos_, pos_, 0, muted_[size_t(t)]);
}

void ArrRecorder::move(int from, int to) {
    const int n = int(open_.size());
    if (from < 0 || to < 0 || from >= n || to >= n || from == to) return;
    moveItem(open_, from, to);
    std::vector<bool> m(muted_.begin(), muted_.end());
    const bool x = m[size_t(from)];
    m.erase(m.begin() + from);
    m.insert(m.begin() + to, x);
    muted_ = m;
    moveItem(played_, from, to);
    moveItem(fired_, from, to);
    moveItem(heard_, from, to);
    for (int t = 0; t < n; ++t)
        if (open_[size_t(t)]) open_[size_t(t)]->track = t;
    for (auto& l : live_) l.track = moved(l.track, from, to);
    for (auto& d : done_) d.track = moved(d.track, from, to);
}

std::vector<ArrRecorder::Seg> ArrRecorder::finish() {
    if (!active_) return {};
    for (int t = 0; t < int(open_.size()); ++t) close(t, pos_ + 1);
    for (auto& o : open_) o.reset();
    active_ = false;
    std::vector<Seg> out = done_;
    done_.clear();
    return out;
}

void ArrRecorder::close(int t, int end) {
    if (!open_[size_t(t)]) return;
    Seg s = *open_[size_t(t)];
    s.end = end;
    if (s.end > s.start) done_.push_back(s);
    open_[size_t(t)].reset();
}

// MARK: — Project::applyRecording

void Project::applyRecording(const std::vector<ArrRecorder::Seg>& segs, const std::vector<std::map<int, int>>& played,
                             const std::vector<std::set<int>>& fired, const std::vector<std::map<int, Step>>& heard,
                             const std::vector<ArrRecorder::LiveNote>& live) {
    for (const auto& s : segs) {
        if (s.track < 0 || s.track >= int(tracks.size()) || s.end <= s.start) continue;
        const int t = s.track;
        tracks[size_t(t)].clearArrange(s.start, s.end);
        std::vector<ArrRecorder::LiveNote> notes;
        for (const auto& l : live)
            if (l.track == t && l.at >= s.start && l.at < s.end) notes.push_back(l);
        if (!(s.slot >= 0 && !s.silent && tracks[size_t(t)].clipFilled(s.slot))) {
            if (!s.silent && !notes.empty()) placeLive(t, s, notes);
            continue;
        }
        const Clip src = *tracks[size_t(t)].clips[size_t(s.slot)];
        if (src.speed() == 0 && t < int(played.size()) && t < int(fired.size())) {
            static const std::map<int, Step> none;
            placeFired(t, s, src, played[size_t(t)], fired[size_t(t)], t < int(heard.size()) ? heard[size_t(t)] : none,
                       notes);
            continue;
        }
        const int pass = tracks[size_t(t)].clipSteps(s.slot);
        tracks[size_t(t)].placeClip(ArrBlock(s.slot, s.start, s.end - s.start, s.offset % pass));
    }
}

namespace {
double levelOf(const Track& t) {
    const auto it = t.params.find("level");
    return it == t.params.end() ? 127 : it->second;
}
Clip emptyClip(int n, int pages) {
    Clip c;
    c.pages = std::vector<Page>(size_t(pages), emptyPage());
    c.length = n;
    c.playFrom = 0;
    c.playTo = pages - 1;
    return c;
}
}  // namespace

void Project::placeFired(int t, const ArrRecorder::Seg& s, const Clip& src, const std::map<int, int>& played,
                         const std::set<int>& fired, const std::map<int, Step>& heard,
                         const std::vector<ArrRecorder::LiveNote>& live) {
    const int maxLen = std::min(Params::maxPages * Params::stepsPerPage, Track::maxClipBlock);
    const int pass = std::max(1, tracks[size_t(t)].clipSteps(s.slot));
    const int chunk = std::min(pass, maxLen);
    const int n16 = Params::stepsPerPage;
    const auto pit = tracks[size_t(t)].params.find("prob");
    const double trackProb =
        pit != tracks[size_t(t)].params.end() ? pit->second : (Params::byId("prob") ? Params::byId("prob")->def : 100);
    for (int x = s.start; x < s.end;) {
        const int ph = (s.offset + x - s.start) % pass;
        const int n = std::min({s.end - x, chunk - ph % chunk, pass - ph});
        const int pages = (n + n16 - 1) / n16;
        Clip c = emptyClip(n, pages);
        c.scale = src.scale;
        c.root = src.root;
        c.fx = src.fx;
        bool any = false;
        for (int i = 0; i < n; ++i) {
            const int at = x + i;
            const auto p = played.find(at);
            if (p == played.end()) continue;
            Step st;
            if (const auto h = heard.find(at); h != heard.end()) st = h->second;
            else if (const Step* q = src.step(p->second)) st = *q;
            else continue;
            if (st.on) {
                if (fired.count(at)) {
                    // Прозвучал — дальше звучит всегда.
                    if (st.locks.count("prob") || trackProb < 100) st.locks["prob"] = 100;
                } else {
                    // Не прозвучал — пустой шаг.
                    st = Step();
                }
            }
            if (st.on) any = true;
            c.pages[size_t(i / n16)][size_t(i % n16)] = st;
        }
        if (addLive(c, live, x, n, levelOf(tracks[size_t(t)]))) any = true;
        if (any) tracks[size_t(t)].placeBlock(ArrBlock(s.slot, x, n, 0, c));
        x += n;
    }
}

void Project::placeLive(int t, const ArrRecorder::Seg& s, const std::vector<ArrRecorder::LiveNote>& live) {
    const int bar = ArrGrid::stepsPerBar;
    const int n16 = Params::stepsPerPage;
    for (int x = s.start; x < s.end;) {
        const int n = std::min(s.end - x, bar - ((x % bar) + bar) % bar);
        const int pages = (n + n16 - 1) / n16;
        Clip c = emptyClip(n, pages);
        if (addLive(c, live, x, n, levelOf(tracks[size_t(t)]))) {
            const int k = std::max(0, std::min(Params::patternSlots - 1, tracks[size_t(t)].slot));
            tracks[size_t(t)].placeBlock(ArrBlock(k, x, n, 0, c));
        }
        x += n;
    }
}

bool Project::addLive(Clip& c, const std::vector<ArrRecorder::LiveNote>& live, int from, int count, double level) {
    bool any = false;
    const int n16 = Params::stepsPerPage;
    for (const auto& ln : live) {
        if (ln.at < from || ln.at >= from + count) continue;
        const int i = ln.at - from;
        Step st = c.pages[size_t(i / n16)][size_t(i % n16)];
        const bool had = st.on;
        if (!had) {
            st.lens.reset();
            st.vels.reset();
        }
        if (had) {
            std::set<int> all(st.notes.begin(), st.notes.end());
            all.insert(ln.notes.begin(), ln.notes.end());
            st.setNotes(std::vector<int>(all.begin(), all.end()));
        } else {
            st.setNotes(ln.notes);
        }
        st.on = true;
        st.setAllLen(std::max(had ? st.len : 1, std::min(ln.len, count - i)));
        if (ln.velocity) {
            const double lv = std::round(level * *ln.velocity);
            for (int note : ln.notes) {
                const auto f = std::find(st.notes.begin(), st.notes.end(), note);
                if (f == st.notes.end()) continue;
                const int k = int(f - st.notes.begin());
                st.setVelLinked(k, std::max(st.noteVel(k).value_or(0), lv), level);
            }
        }
        c.pages[size_t(i / n16)][size_t(i % n16)] = st;
        any = true;
    }
    return any;
}

// MARK: — свои копии клипов у блоков (Arrange.swift)

std::optional<Clip> Track::blockClip(const ArrBlock& b) const {
    if (b.pattern) return b.pattern;
    if (b.clip >= 0 && b.clip < int(clips.size())) return clips[size_t(b.clip)];
    return std::nullopt;
}

bool Track::ownArrPatterns() {
    bool changed = false;
    for (auto& b : blocks) {
        if (b.pattern) continue;
        const int k = b.clip;
        if (k < 0 || k >= int(clips.size()) || !clips[size_t(k)]) continue;
        Clip copy = *clips[size_t(k)];
        // Звук перехода MIX (PREV) — у клипа сетки, не у блока.
        copy.src.reset();
        b.pattern = copy;
        changed = true;
    }
    return changed;
}

bool Project::ownArrPatterns() {
    bool changed = false;
    for (auto& t : tracks) {
        if (t.blocks.empty()) continue;
        const bool need = std::any_of(t.blocks.begin(), t.blocks.end(), [](const ArrBlock& b) { return !b.pattern; });
        if (need && t.ownArrPatterns()) changed = true;
    }
    return changed;
}

}  // namespace jambo
