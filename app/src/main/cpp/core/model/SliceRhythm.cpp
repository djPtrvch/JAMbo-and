// Случайные партии из маркеров нарезки (SliceRhythm.swift).
#include "SliceRhythm.h"
#include "DrumRhythm.h"
#include "Project.h"
#include "sampler/SampleData.h"
#include <cmath>
#include <string_view>

namespace jambo::SliceRhythm {
constexpr int perPage = Params::stepsPerPage;

std::vector<SlicePiece> slicePieces(const Track& t, SampleData& sample, const Project& project) {
    auto marks = t.sliceMarks(t.sample);
    const auto notes = Slices::notes(marks, SliceMap::chromatic, project.scale, project.root);
    const bool noted = std::any_of(marks.begin(), marks.end(), [](const auto& m) { return !m.isFree(); });
    std::vector<size_t> indexes;
    for (size_t i = 0; i < marks.size(); ++i)
        if (marks[i].isFree() != noted) indexes.push_back(i);
    std::sort(indexes.begin(), indexes.end(), [&](size_t a, size_t b) { return marks[a].at < marks[b].at; });
    const auto info = t.warps.find(t.sample);
    std::optional<WarpMap> warp;
    if (info != t.warps.end() && !info->second.isOff() && !info->second.marks.empty()) {
        const double bpm = WarpMap::bpm(&info->second, info->second.bpm ? 0 : sample.loopBpm());
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
        out.push_back({noted ? notes[k] : Slices::noNote, std::max(1, int(std::ceil(beats * 4))), a, b, role});
    }
    return out;
}

namespace {
using Rng = std::mt19937;

int between(Rng& rng, int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

bool chance(Rng& rng, int percent) {
    return between(rng, 1, 100) <= percent;
}

struct SliceEvent {
    int step, piece, length, velocity;
};

std::vector<SliceEvent> sliceEvents(const std::vector<SlicePiece>& pieces, int len, int style, Rng& rng) {
    std::vector<SliceEvent> events;
    const auto byRole = [&](int role) {
        std::vector<int> indexes;
        for (size_t i = 0; i < pieces.size(); ++i) if (pieces[i].role == role) indexes.push_back(int(i));
        return indexes;
    };
    const auto pick = [&](const std::vector<int>& indexes) {
        return indexes[size_t(between(rng, 0, int(indexes.size()) - 1))];
    };
    const auto velocity = [&](int step) { return step % 4 == 0 ? 127 : between(rng, 89, 108); };
    const bool kit = std::any_of(pieces.begin(), pieces.end(), [](const auto& p) { return p.role >= 0 && p.role != 3; });
    if (kit) {
        constexpr int roles[] = {0, 1, 2, 5, 6, 7};
        for (int role : roles) {
            const auto own = byRole(role);
            if (own.empty()) continue;
            std::vector<int> hits;
            if (role <= 2 || role == 5) {
                hits = DrumRhythm::drumBase(role == 5 ? int(DrumRhythm::perc) : role, style, len, rng);
            } else {
                constexpr const char* bass[] = {"26ae", "26ae", "136be", "0a", "02468ace", "0"};
                constexpr const char* lead[] = {"2369ae", "236ae", "1367be", "0368b", "0248c", "08"};
                for (char c : std::string_view(role == 6 ? bass[style] : lead[style])) {
                    const int step = c <= '9' ? c - '0' : c - 'a' + 10;
                    if (step < len) hits.push_back(step);
                }
            }
            for (int step : hits) events.push_back({step, pick(own), 1, velocity(step)});
        }
    } else {
        const auto unassigned = byRole(-1);
        for (int step = 0, next = 0; !unassigned.empty() && step < len;) {
            const int piece = step > 0 && chance(rng, 30) ? pick(unassigned) : unassigned[size_t(next)];
            const int length = std::min(pieces[size_t(piece)].length, len - step);
            events.push_back({step, piece, length, velocity(step)});
            step += length;
            next = int((std::find(unassigned.begin(), unassigned.end(), piece) - unassigned.begin() + 1) % unassigned.size());
        }
    }
    const auto pads = byRole(3);
    for (int step = 0; !pads.empty() && step < len;) {
        const int piece = pick(pads);
        const int gap = pieces[size_t(piece)].length <= 8 ? 8 : perPage;
        events.push_back({step, piece, std::min({pieces[size_t(piece)].length, gap, len - step}), 127});
        step += gap;
    }
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return std::tie(a.step, a.piece) < std::tie(b.step, b.piece);
    });
    return events;
}

void writeSlices(Page& page, const std::vector<SlicePiece>& pieces, const std::vector<SliceEvent>& events,
                 const Track& t, int note) {
    const auto start = t.params.find("start"), end = t.params.find("end");
    for (const auto& event : events) {
        const auto& piece = pieces[size_t(event.piece)];
        Step& step = page[size_t(event.step)];
        if (piece.note == Slices::noNote) {
            if (step.on) continue;
            if (std::abs(piece.start * 100 - (start == t.params.end() ? 0 : start->second)) > 0.0001)
                step.locks["start"] = piece.start * 100;
            if (std::abs(piece.end * 100 - (end == t.params.end() ? 100 : end->second)) > 0.0001)
                step.locks["end"] = piece.end * 100;
        }
        step.on = true;
        step.addNote(piece.note == Slices::noNote ? note : piece.note, event.length, event.velocity);
        step.locks["level"] = std::max(step.locks["level"], double(event.velocity));
    }
}

}  // namespace

void writePage(Page& page, const std::vector<SlicePiece>& pieces, const Track& track, int note,
               int len, int style, std::vector<int>* last, Rng& rng) {
    auto events = sliceEvents(pieces, len, style, rng);
    if (last) {
        std::vector<int> code;
        for (int attempt = 0; attempt < 16; ++attempt) {
            code.clear();
            for (const auto& e : events) code.push_back(e.step * 128 + e.piece);
            if (code != *last || attempt == 15) break;
            events = sliceEvents(pieces, len, style, rng);
        }
        *last = std::move(code);
    }
    writeSlices(page, pieces, events, track, note);
}

}  // namespace jambo::SliceRhythm
