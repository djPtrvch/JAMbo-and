// Случайные партии выбранной дорожки и ансамбля (AppModelChords, AppModelEnsemble).
#include "App.h"
#include "model/DrumRhythm.h"
#include "model/MelodyRhythm.h"
#include "model/Scales.h"
#include "model/SliceRhythm.h"
#include "model/SynthEngines.h"
#include <variant>

namespace jambo {
namespace {

using MelodyRhythm::Role;
using namespace RhythmRandom;
constexpr int per = Params::stepsPerPage;

std::string fileName(const Project& p, const Track& t) {
    const auto it = p.files.find(t.sample);
    return it == p.files.end() ? std::string{} : it->second;
}

int drumRole(const Project& p, const Track& t) {
    const int engineRole = t.drumRole();
    if (engineRole >= 0 || t.sample.empty() || SynthKey::engine(t.sample) >= 0 || t.hasCuts()) return engineRole;
    return MelodyRhythm::drumRoleByName(fileName(p, t));
}

PageRange selectedPages(const Clip& clip, int visible) {
    if (clip.playFrom >= 0 && clip.playFrom <= clip.playTo && clip.playTo < int(clip.pages.size()))
        return {clip.playFrom, clip.playTo};
    const int p = std::clamp(visible, 0, int(clip.pages.size()) - 1);
    return {p, p};
}

int drumNote(const Track& t, int root, int octave) {
    return SynthKey::engine(t.sample) >= 0 ? Scales::baseNote(root, octave)
        : Params::sampleRoot + Scales::wrap(root + 6, 12) - 6;
}

MelodyRhythm::Kicks kickGrid(const Project& p, PageRange range, int style) {
    const auto fallback = MelodyRhythm::defaultKick(style);
    MelodyRhythm::Kicks out(size_t(range.upper - range.lower + 1), fallback);
    for (const auto& track : p.tracks) {
        if (drumRole(p, track) != DrumRhythm::kick) continue;
        const auto& clip = track.openClip();
        const bool triggers = std::any_of(clip.pages.begin(), clip.pages.end(), [](const Page& page) {
            return std::any_of(page.begin(), page.end(), [](const Step& s) { return s.on; });
        });
        if (!triggers) continue;
        const int count = std::clamp((clip.length + per - 1) / per, 1, int(clip.pages.size()));
        for (size_t j = 0; j < out.size(); ++j) {
            const int base = (range.lower + int(j)) % count * per;
            std::set<int> steps;
            for (int s = 0; s < per && base + s < clip.length; ++s) {
                const Step* step = clip.step(base + s);
                if (step && step->on) steps.insert(s);
            }
            if (!steps.empty()) out[j] = std::move(steps);
        }
        break;
    }
    return out;
}

struct Drums {
    int role, note;
};
struct SlicedSample {
    std::vector<SliceRhythm::SlicePiece> pieces;
    int note;
};
struct Job {
    size_t track;
    PageRange range;
    std::variant<Drums, SlicedSample, MelodyRhythm::Part> part;
};
}

void App::setRandomSettings(int options) {
    randomSettings_ = options & (randomChords | randomDrums | randomSamples | randomSwing | randomAll | showRandomPart);
    bump();
}

int App::randomDrumRole(const Track& t) const { return drumRole(project_, t); }

bool App::randomSlices(const Track& t) const {
    return (randomSettings_ & randomSamples) && t.hasCuts() && t.drumRole() < 0;
}

bool App::randomButton() const {
    return channel_ == Channel::track && !arp().mode &&
        (((randomSettings_ & randomDrums) && randomDrumRole(track()) >= 0) || randomSlices(track()));
}

int App::chordStyle() const {
    if (channel_ != Channel::track || arp().mode || (randomDrumRole(track()) < 0 && !randomSlices(track()))) return -1;
    return DrumRhythm::styleIndex(track().drumStyle);
}

int App::randomPartOverride(size_t track) const {
    return randomSettings_ & showRandomPart ? randomParts_[track] : -1;
}

int App::randomPart() const {
    if (channel_ != Channel::track || arp().mode || chordStyle() >= 0 || !(randomSettings_ & showRandomPart)) return -1;
    const auto& t = track();
    const auto role = MelodyRhythm::role(SynthKey::engine(t.sample), fileName(project_, t), octave(), chordOn(),
                                        false, randomPartOverride(size_t(selected_)));
    return role == Role::bass ? 0 : role == Role::pad ? 2 : 1;
}

void App::chooseRandomPart(int part) {
    if (randomPart() < 0 || part < 0 || part > 2) return;
    auto& selectedPart = randomParts_[size_t(selected_)];
    selectedPart = selectedPart == part ? -1 : part;
    bump();
}

void App::writeRandom() {
    static Rng rng(std::random_device{}());
    const bool all = (randomSettings_ & randomAll) != 0;
    const int style = DrumRhythm::styleIndex(track().drumStyle);
    const int scale = project_.scale, root = project_.root;
    const auto* last = lastProgression_.empty() ? nullptr : &lastProgression_;
    Ints degrees;
    int kind = 0;
    const auto technoKind = [&](bool fresh) {
        if (fresh || !lastRandomKind_) lastRandomKind_ = MelodyRhythm::kind(1, rng);
        return *lastRandomKind_;
    };
    if (all) {
        degrees = MelodyRhythm::progression(style, scale, last, rng);
        kind = style == 1 ? technoKind(true) : MelodyRhythm::kind(style, rng);
        lastProgression_ = degrees;
    }
    std::vector<Job> jobs;
    const auto range = selectedPages(track().openClip(), viewPage());
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& t = project_.tracks[i];
        const bool selected = i == size_t(selected_);
        if ((!selected && !all) || (all && t.sample.empty())) continue;
        const int oct = selected ? octave() : std::min(topOctave(), t.keyOctave);
        const auto pages = selected ? range : t.openClip().range();
        const int drum = randomDrumRole(t);
        if (drum >= 0) {
            if (randomSettings_ & randomDrums) jobs.push_back({i, pages, Drums{drum, drumNote(t, root, oct)}});
        } else if (randomSlices(t)) {
            const auto sample = sampleData(t.sample);
            if (sample) {
                auto pieces = SliceRhythm::slicePieces(t, *sample, project_, sliceMap_, warpFollow_);
                if (!pieces.empty()) jobs.push_back({i, pages, SlicedSample{std::move(pieces), drumNote(t, root, oct)}});
            }
        } else if (randomSettings_ & randomChords) {
            const bool chord = chordMode_[i];
            const Role role = MelodyRhythm::role(SynthKey::engine(t.sample), fileName(project_, t), oct,
                                                 chord, t.arp.mode, randomPartOverride(i));
            if (!all) {
                const bool fresh = role == Role::keys || role == Role::arp;
                if (style == 1) {
                    degrees = !fresh && last && MelodyRhythm::isTechnoProgression(*last) ? *last
                        : MelodyRhythm::progression(style, scale, last, rng);
                    if (role != Role::pad) lastProgression_ = degrees;
                    kind = technoKind(fresh);
                } else {
                    degrees = !fresh && role != Role::pad && last ? *last
                        : MelodyRhythm::progression(style, scale, last, rng);
                    lastProgression_ = degrees;
                    kind = MelodyRhythm::kind(style, rng);
                }
            }
            jobs.push_back({i, pages, MelodyRhythm::Part{role, style, kind, scale, root, oct, !chord, t.arp}});
        }
    }
    if (jobs.empty()) return;
    std::stable_partition(jobs.begin(), jobs.end(), [](const Job& job) {
        return !std::holds_alternative<MelodyRhythm::Part>(job.part);
    });
    edit([&](Project& p) {
        for (const auto& job : jobs) {
            auto& t = p.tracks[job.track];
            const int trackStyle = DrumRhythm::styleIndex(t.drumStyle);
            Ints lengths;
            for (int pg = job.range.lower; pg <= job.range.upper; ++pg) lengths.push_back(t.pageLen(pg, job.range.lower));
            auto& pages = t.pagesMut();
            for (int pg = job.range.lower; pg <= job.range.upper; ++pg) pages[size_t(pg)] = emptyPage();
            bool autoSwing = (randomSettings_ & randomSwing) != 0;
            if (const auto* drums = std::get_if<Drums>(&job.part)) {
                auto& past = lastDrumPattern_[t.id];
                const auto hits = DrumRhythm::pattern(drums->role, trackStyle, lengths, &past, rng);
                const auto first = std::find_if(hits.begin(), hits.end(), [](const auto& page) { return !page.empty(); });
                past = first == hits.end() ? Ints{} : DrumRhythm::steps(*first);
                for (size_t j = 0; j < hits.size(); ++j)
                    DrumRhythm::writePage(pages[size_t(job.range.lower) + j], hits[j], drums->note);
            } else if (const auto* slices = std::get_if<SlicedSample>(&job.part)) {
                auto& past = lastDrumPattern_[-1 - t.id];
                const auto events = SliceRhythm::pattern(slices->pieces, lengths, trackStyle, &past, rng);
                const auto first = std::find_if(events.begin(), events.end(), [](const auto& page) { return !page.empty(); });
                past = first == events.end() ? Ints{} : SliceRhythm::code(*first);
                for (size_t j = 0; j < events.size(); ++j)
                    SliceRhythm::writePage(pages[size_t(job.range.lower) + j], slices->pieces, events[j], t, slices->note);
            } else {
                const auto& part = std::get<MelodyRhythm::Part>(job.part);
                const auto events = MelodyRhythm::generate(part, lengths, degrees, kickGrid(p, job.range, style), rng);
                const bool technoArp = style == 1 && part.role == Role::arp;
                for (size_t j = 0; j < events.size(); ++j)
                    SliceRhythm::writePage(pages[size_t(job.range.lower) + j], {}, events[j], t, 0, technoArp);
                autoSwing &= part.role != Role::pad && (part.role != Role::arp || (all && technoArp));
            }
            if (autoSwing) t.params["swing"] = DrumRhythm::swing[trackStyle];
        }
    });
}
}
