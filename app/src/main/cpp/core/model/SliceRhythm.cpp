// Случайные партии из маркеров нарезки (SliceRhythm.swift).
#include "SliceRhythm.h"
#include "DrumRhythm.h"
#include "Project.h"
#include "sampler/SampleData.h"
#include <cmath>
#include <tuple>


namespace jambo::SliceRhythm {
constexpr int perPage = Params::stepsPerPage;
constexpr int cutNote = 1000;
using namespace RhythmRandom;

std::vector<SlicePiece> slicePieces(const Track& t, SampleData& sample, const Project& project, SliceMap map, bool follow) {
    auto marks = t.sliceMarks(t.sample);
    const auto notes = Slices::notes(marks, map, project.scale, project.root);
    const bool noted = std::any_of(marks.begin(), marks.end(), [](const auto& m) { return !m.isFree(); });
    std::vector<size_t> indexes;
    for (size_t i = 0; i < marks.size(); ++i)
        if (marks[i].isFree() != noted) indexes.push_back(i);
    std::stable_sort(indexes.begin(), indexes.end(), [&](size_t a, size_t b) { return marks[a].at < marks[b].at; });
    const auto info = t.warps.find(t.sample);
    std::optional<WarpMap> warp;
    if (info != t.warps.end() && !info->second.isOff() && !info->second.isEmpty()) {
        const double bpm = WarpMap::bpm(&info->second, info->second.bpm && !follow ? 0 : sample.loopBpm(),
                                        follow ? std::optional<double>(project.bpm) : std::nullopt);
        warp.emplace(info->second, sample.frames(), bpm);
    }
    std::vector<SlicePiece> out;
    for (size_t i = 0; i < indexes.size(); ++i) {
        const auto k = indexes[i];
        const double a = marks[k].at;
        const double b = i + 1 < indexes.size() ? marks[indexes[i + 1]].at : 1;
        const double beats = warp ? warp->beat(b * sample.frames()) - warp->beat(a * sample.frames())
                                  : (b - a) * sample.duration() * project.bpm / 60;
        int role = marks[k].role.value_or(-1);
        if (role == 4) role = 1;
        if (role < 0 || role > 7) role = -1;
        out.push_back({noted ? notes[k] : cutNote + int(i), std::max(1, int(std::ceil(beats * 4))), a, b, role});
    }
    return out;
}

namespace {
using Pieces = std::vector<const SlicePiece*>;
using Events = std::vector<RhythmNote>;

Pieces byRole(const std::vector<SlicePiece>& pieces, int role) {
    Pieces out;
    for (const auto& p : pieces) if (p.role == role) out.push_back(&p);
    return out;
}

double velocity(int step, Rng& rng) {
    return step % 4 == 0 ? 127.0 : std::round(real(rng, 88.9, 107.95));
}

Events clipped(Events events, int length) {
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.step < b.step; });
    for (size_t i = 0; i < events.size(); ++i) {
        auto& event = events[i];
        const int end = i + 1 < events.size() ? events[i+1].step : length;
        event.length = std::max(1, std::min(event.length, end - event.step));
    }
    return events;
}

Events breakBase(const Pieces& pieces, int length, Rng& rng) {
    Events out;
    for (int s = 0, next = 0; s < length;) {
        const int i = s > 0 && pieces.size() > 1 && real(rng) < 0.3
            ? integer(rng, 0, int(pieces.size()) - 1) : next % int(pieces.size());
        const auto& p = *pieces[size_t(i)];
        const int len = std::min(p.length, length - s);
        out.push_back({s, p.note, len, velocity(s, rng)});
        s += len;
        next = i + 1;
    }
    return out;
}

Events breakVary(const Events& base, const Pieces& pieces, int length, Rng& rng) {
    Events out;
    for (const auto& event : base) if (event.step < length) out.push_back(event);
    if (out.size() < 2 || pieces.size() < 2) return clipped(std::move(out), length);
    const int changes = integer(rng, 1, 2);
    for (int j = 0; j < changes; ++j) {
        auto& event = out[size_t(integer(rng, 1, int(out.size()) - 1))];
        Pieces others;
        for (const auto* p : pieces) if (p->note != event.note) others.push_back(p);
        if (others.empty()) continue;
        const auto& p = *pick(rng, others);
        event.note = p.note;
        event.length = std::min(event.length, p.length);
    }
    return clipped(std::move(out), length);
}

Events breakFill(const Events& base, const Pieces& pieces, int length, Rng& rng) {
    const int from = std::max(0, length - 4);
    Events out;
    for (auto event : base) {
        if (event.step >= from) continue;
        event.length = std::min(event.length, from - event.step);
        out.push_back(event);
    }
    const auto* repeat = pick(rng, pieces);
    const auto before = out.size();
    for (int s = from; s < length; ++s) {
        if (real(rng) >= 0.6) continue;
        const auto* p = real(rng) < 0.6 ? repeat : pick(rng, pieces);
        out.push_back({s, p->note, 1, velocity(s, rng)});
    }
    if (out.size() == before) {
        const int s = integer(rng, from, length - 1);
        out.push_back({s, repeat->note, 1, velocity(s, rng)});
    }
    return clipped(std::move(out), length);
}

void kit(RhythmPattern& out, const std::vector<SlicePiece>& pieces, const Ints& lengths, int style, Rng& rng) {
    for (int role : {0, 1, 2, 5, 6, 7}) {
        const auto own = byRole(pieces, role);
        if (own.empty()) continue;
        if (role <= 2 || role == 5) {
            const auto hits = DrumRhythm::pattern(role == 5 ? int(DrumRhythm::perc) : role, style, lengths, nullptr, rng);
            for (size_t p = 0; p < out.size(); ++p)
                for (const auto& h : hits[p]) out[p].push_back({h.step, pick(rng, own)->note, 1, h.velocity.value_or(127)});
        } else {
            using MelodyRhythm::Role;
            const auto part = role == 6 ? Role::bass : coin(rng) ? Role::lead : Role::keys;
            const auto events = MelodyRhythm::generate({part, style, 0, 0, 0,
                style == 1 && part != Role::bass ? 4 : 3, false, {}}, lengths, {1, 1, 1, 1}, {}, rng);
            for (size_t p = 0; p < out.size(); ++p) {
                for (const auto& event : events[p]) {
                    const auto* piece = pick(rng, own);
                    out[p].push_back({event.step, piece->note, std::min(event.length, piece->length), event.velocity});
                }
            }
        }
    }
}
}  // namespace

Ints code(const std::vector<RhythmNote>& events) {
    Ints out;
    for (const auto& e : events) out.push_back(e.step * 128 + e.note);
    return out;
}

RhythmPattern pattern(const std::vector<SlicePiece>& pieces, const Ints& lengths, int style, const Ints* last, Rng& rng) {
    RhythmPattern out(lengths.size());
    const auto playing = MelodyRhythm::Detail::playingPages(lengths);
    if (playing.empty()) return out;
    const bool kitMode = std::any_of(pieces.begin(), pieces.end(), [](const auto& p) { return p.role >= 0 && p.role != 3; });
    if (kitMode) kit(out, pieces, lengths, style, rng);
    else {
        const auto unassigned = byRole(pieces, -1);
        if (!unassigned.empty()) {
            const auto first = size_t(playing.front()), end = size_t(playing.back());
            auto base = breakBase(unassigned, lengths[first], rng);
            for (int tries = 1; last && code(base) == *last && tries < 16; ++tries)
                base = breakBase(unassigned, lengths[first], rng);
            out[first] = base;
            for (int p : playing)
                if (size_t(p) != first && size_t(p) != end) out[size_t(p)] = breakVary(base, unassigned, lengths[size_t(p)], rng);
            if (end != first) out[end] = breakFill(base, unassigned, lengths[end], rng);
        }
    }
    const auto pads = byRole(pieces, 3);
    if (!pads.empty()) {
        for (int p : playing) {
            for (int s = 0; s < lengths[size_t(p)];) {
                const auto* piece = pick(rng, pads);
                const int gap = piece->length <= 8 ? 8 : perPage;
                out[size_t(p)].push_back({s, piece->note, std::min({piece->length, gap, lengths[size_t(p)] - s}), 127});
                s += gap;
            }
        }
    }
    for (auto& events : out) {
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return std::tie(a.step, a.note) < std::tie(b.step, b.note);
        });
    }
    return out;
}

void writePage(Page& page, const std::vector<SlicePiece>& pieces, const std::vector<RhythmNote>& events,
               const Track& t, int note, bool arpSteps) {
    const auto start = t.params.find("start"), end = t.params.find("end");
    std::set<int> cutSteps;
    for (const auto& e : events) {
        Step& step = page[size_t(e.step)];
        if (arpSteps) {
            step.on = true;
            step.notes = {e.note};
            step.setAllLen(e.length);
            step.locks["level"] = e.velocity;
        } else if (e.note < cutNote) {
            step.on = true;
            step.addNote(e.note, e.length, e.velocity);
        } else if (cutSteps.insert(e.step).second) {
            const auto& piece = pieces[size_t(e.note - cutNote)];
            step.on = true;
            step.addNote(note, e.length, e.velocity);
            if (std::abs(piece.start * 100 - (start == t.params.end() ? 0 : start->second)) > 0.0001)
                step.locks["start"] = piece.start * 100;
            if (std::abs(piece.end * 100 - (end == t.params.end() ? 100 : end->second)) > 0.0001)
                step.locks["end"] = piece.end * 100;
        }
        step.locks["level"] = std::max(step.locks["level"], e.velocity);
    }
}
}  // namespace jambo::SliceRhythm
