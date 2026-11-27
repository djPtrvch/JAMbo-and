// TechnoRun.swift, TechnoParts.swift и TechnoKeys.swift: ритм и фигура идут через весь клип.
#include "MelodyRhythm.h"
#include "Chords.h"
#include "DrumRhythm.h"
#include "Scales.h"
#include <cmath>

namespace jambo::MelodyRhythm::Detail {
using namespace RhythmRandom;

namespace {
using Mask = std::vector<bool>;
using Rows = std::vector<std::vector<Ints>>;

Mask euclidCell(int k, int n, int rotation) {
    Mask cell(size_t(n), false);
    for (int step : DrumRhythm::euclid(k, n, rotation)) cell[size_t(step)] = true;
    return cell;
}

Mask technoMask(int perBar, bool dense, Rng& rng) {
    Mask all(size_t(perBar), true);
    const double r = real(rng);
    if (perBar < 4 || (dense && r < 0.2)) return all;
    const int every = std::max(1, per / perBar);
    Ints free;
    for (int s = 0; s < perBar; ++s) if ((s * every) % 4 != 0) free.push_back(s);
    const int need = std::max(1, int(free.size()) / (dense ? 2 : 4));
    const bool bar = r < (dense ? 0.6 : 0.4);
    for (int attempt = 0; attempt < 8; ++attempt) {
        Mask cell;
        if (bar) {
            const int lo = std::max(2, perBar * (dense ? 9 : 5) / 16);
            const int hi = std::max(lo, perBar * (dense ? 13 : 9) / 16);
            const int k = integer(rng, lo, hi);
            cell = euclidCell(k, perBar, integer(rng, 0, perBar - 1));
        } else {
            Ints sizes;
            for (int n : {3, 5, 6, 7}) if (n < perBar) sizes.push_back(n);
            const int n = sizes.empty() ? 3 : pick(rng, sizes);
            const int k = integer(rng, std::max(2, (n + 1) / 2), n - 1);
            cell = euclidCell(k, n, integer(rng, 0, n - 1));
        }
        bool ok = true;
        for (int b = 0; b < 4; ++b) {
            int count = 0;
            for (int s : free) if (cell[size_t(b * perBar + s) % cell.size()]) ++count;
            if (count < need) ok = false;
        }
        if (ok) return cell;
    }
    return all;
}

Ints technoFigure(int cycle, int width, bool random, Rng& rng) {
    const int w = std::max(1, width);
    const double r = real(rng);
    Ints out;
    for (int j = 0; j < cycle; ++j) {
        if (random) out.push_back(integer(rng, 0, w - 1));
        else if (r < 0.5 || w < 3) out.push_back(j % w);
        else if (r < 0.75) out.push_back(j % 2 == 0 ? 0 : 1 + (j / 2) % (w - 1));
        else out.push_back((j / 2 + (j % 2 == 1 ? 2 : 0)) % w);
    }
    return out;
}

const std::set<int>& kickOf(const Kicks& kicks, int page) {
    static const auto fallback = defaultKick(1);
    return page < int(kicks.size()) ? kicks[size_t(page)] : fallback;
}

RhythmPattern run(const Ints& lengths, const Rows& rows, const Ints& figure, const Mask& cell, int every,
                  const Kicks& kicks, bool rootFirst, std::optional<double> gate, Rng& rng) {
    RhythmPattern out(lengths.size());
    const auto playing = playingPages(lengths);
    if (playing.empty() || rows.empty() || figure.empty() || cell.empty()) return out;
    const int count = int(playing.size());
    const int total = (count - 1) * per + lengths[size_t(playing.back())];
    const int slots = (total + every - 1) / every, perBar = std::max(1, per / every);
    Mask mask;
    for (int i = 0; i < slots; ++i) mask.push_back(cell[size_t(i) % cell.size()]);
    for (int pi = 1; pi < count - 1; ++pi) {
        const int a = pi * perBar, b = std::min(slots, a + perBar);
        if (b - a < 2) continue;
        const auto& kick = kickOf(kicks, playing[size_t(pi)]);
        const bool want = coin(rng);
        Ints candidates;
        for (int j = a + 1; j < b; ++j) {
            const int step = j * every % per;
            if (mask[size_t(j)] == want && !kick.count(step) && step % 4 != 2) candidates.push_back(j);
        }
        if (!candidates.empty()) mask[size_t(pick(rng, candidates))].flip();
    }
    int hit = 0, fill = 0, lastPage = -1, lastAbs = -1;
    const int gateLength = gate ? std::max(1, int(std::round(Arp::clampGate(*gate) * every))) : 1;
    for (int slot = 0; slot < slots; ++slot) {
        if (!mask[size_t(slot)]) continue;
        const int s = slot * every, pi = s / per, step = s % per, page = playing[size_t(pi)];
        if (step >= lengths[size_t(page)] || kickOf(kicks, page).count(step)) continue;
        if (pi == count - 1 && count > 1 && step >= 12 && ++fill % 2 == 0) continue;
        const int pos = hit++ % int(figure.size());
        const auto& row = rows[count == 1 ? 0 : size_t(pi) * rows.size() / size_t(count)];
        if (row.empty()) continue;
        double velocity = integer(rng, 70, 95);
        if (pos == 0 && figure.size() > 1) velocity = integer(rng, 100, 114);
        if (step % 4 == 2) velocity = 127;
        const bool first = rootFirst && pi != lastPage;
        const int absolute = page * per + step;
        if (gate && lastPage >= 0) {
            auto& prior = out[size_t(playing[size_t(lastPage)])];
            prior.back().length = std::max(1, std::min(gateLength, absolute - lastAbs));
        }
        lastPage = pi;
        lastAbs = absolute;
        for (int note : row[first ? 0 : size_t(figure[size_t(pos)]) % row.size()])
            out[size_t(page)].push_back({step, note, gateLength, velocity});
    }
    if (gate && lastPage >= 0) {
        const int end = playing.back() * per + lengths[size_t(playing.back())];
        out[size_t(playing[size_t(lastPage)])].back().length = std::max(1, std::min(gateLength, end - lastAbs));
    }
    return out;
}

Mask weightedCell(std::string_view cells, const std::vector<double>& weights, Rng& rng) {
    const auto rows = split(cells);
    Mask cell(per, false);
    for (int s : digits(rows[weighted(rng, weights)])) cell[size_t(s)] = true;
    return cell;
}

RhythmPattern pad(const Part& part, const Ints& lengths, int n, Rng& rng) {
    RhythmPattern out(lengths.size());
    const auto playing = playingPages(lengths);
    if (playing.empty()) return out;
    const Ints offs[] = {{2, 4, 6}, {2, 4, 6, 8}, {4, 6}};
    const auto progressions = split("1767 1671 1776 1617 1716 1667");
    const int count = int(playing.size());
    const int span = count >= 2 && coin(rng) ? 2 : 1;
    const auto chords = digits(pick(rng, progressions));
    Ints degrees;
    std::vector<Ints> voices;
    std::optional<int> prev;
    for (int i = 0; i < count; ++i) {
        const int d = chords[size_t(i / span) % chords.size()];
        degrees.push_back(d);
        const int root = Chords::degreeIndex(d, part.scale);
        auto upper = closeVoicing(root, offs[part.kind], n, prev);
        Ints row{root};
        if (!part.single) for (int x : upper) {
            int y = x + n;
            while (y <= root) y += n;
            row.push_back(y);
        }
        voices.push_back(std::move(row));
        prev = upper.back();
    }
    const int end = (count - 1) * per + lengths[size_t(playing.back())];
    for (int pi = 0; pi < count;) {
        int pj = pi;
        while (pj + 1 < count && degrees[size_t(pj + 1)] == degrees[size_t(pi)]) ++pj;
        const int b = std::min(end, (pj + 1) * per);
        for (int s = pi * per; s < b; s += 2 * per) {
            const bool first = s == pi * per;
            const double velocity = first ? integer(rng, 96, 107) : integer(rng, 77, 95);
            for (int degree : voices[size_t(pi)]) {
                int pitch = Scales::note(part.scale, part.root, part.octave, degree);
                while (pitch > 127) pitch -= 12;
                if (pitch >= 0) out[size_t(playing[size_t(s / per)])].push_back(
                    {s % per, pitch, std::min(2 * per, b - s), velocity});
            }
        }
        pi = pj + 1;
    }
    return out;
}
}  // namespace

RhythmPattern techno(const Part& part, const Ints& lengths, const Ints& degrees, const Kicks& kicks, Rng& rng) {
    const int n = int(Scales::all[size_t(part.scale)].steps.size());
    if (part.role == Role::pad) return pad(part, lengths, n, rng);
    Rows rows;
    Mask cell;
    Ints fig;
    int every = 1;
    std::optional<double> gate;
    if (part.role == Role::bass) {
        cell = weightedCell("26ae 236abe 1235679abdef 2367abef 256ade 267aef 12569ade 2356abde",
                            {5, 15, 20, 20, 5, 10, 10, 15}, rng);
        for (int d : degrees) {
            const int k = Chords::degreeIndex(d, part.scale);
            rows.push_back({{note(k, part)}, {note(k + n, part)}, {note(std::max(0, k - 1), part)}});
        }
        fig.assign(8, 0);
        if (coin(rng)) {
            const int at = integer(rng, 1, 7);
            fig[size_t(at)] = integer(rng, 1, 2);
        }
    } else if (part.role == Role::lead) {
        const int count = integer(rng, 2, 3), hi = n + n / 2;
        const Ints pool{n, 4, 6, 1};
        Ints pitches{0};
        for (int i = 0; i < 20 && int(pitches.size()) < count; ++i) {
            const int d = pool[weighted(rng, {0.4, 0.3, 0.15, 0.15})];
            if (std::find(pitches.begin(), pitches.end(), d) == pitches.end()) pitches.push_back(d);
        }
        for (int d : degrees) {
            const int k = Chords::degreeIndex(d, part.scale);
            std::vector<Ints> row;
            for (int off : pitches) {
                int x = k + off;
                while (x > hi) x -= n;
                row.push_back({note(std::max(0, x), part)});
            }
            rows.push_back(std::move(row));
        }
        fig.resize(size_t(integer(rng, 3, 7)));
        for (int& i : fig) i = real(rng) < 0.55 ? 0 : integer(rng, 1, int(pitches.size()) - 1);
        if (std::none_of(fig.begin(), fig.end(), [](int i) { return i != 0; }))
            fig[size_t(integer(rng, 1, int(fig.size()) - 1))] = 1;
        cell = technoMask(per, false, rng);
    } else if (part.role == Role::keys) {
        const Ints offsets[] = {{2, 4, 6}, {4, 6, 8}, {4, 6, 7}};
        std::optional<int> prev;
        for (int d : degrees) {
            auto voices = closeVoicing(Chords::degreeIndex(d, part.scale), offsets[part.kind], n, prev);
            prev = voices.back();
            for (int& degree : voices) degree = note(degree, part);
            rows.push_back({std::move(voices)});
        }
        cell = weightedCell("2a 3a 26ae 25ad 36be 26b 6e", {20, 15, 15, 15, 15, 10, 10}, rng);
        fig = {0};
    } else {
        const Ints offsets = part.kind == 1 ? Ints{0, 4, 6, 8} : Ints{0, 4, 6};
        const auto& a = part.arp;
        for (int d : degrees) {
            Ints tones;
            for (int off : offsets) tones.push_back(note(Chords::degreeIndex(d, part.scale) + off, part));
            std::vector<Ints> row;
            for (int tone : arpSequence(std::move(tones), a.dir, a.octaves, 12)) row.push_back({tone});
            rows.push_back(std::move(row));
        }
        const int width = rows.empty() ? 1 : int(rows.front().size());
        fig = technoFigure(integer(rng, 3, 7), width, a.dir == Arp::random, rng);
        constexpr int grid[] = {2, 2, 1, 1, 1};
        every = grid[Arp::clampRate(a.rate)];
        cell = technoMask(per / every, true, rng);
        gate = a.gate;
    }
    return run(lengths, rows, fig, cell, every, kicks, part.role == Role::bass, gate, rng);
}
}
