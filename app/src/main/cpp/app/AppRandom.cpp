// RANDOM: стили DrumRhythm, партии по гамме и общая прогрессия ансамбля.
#include "App.h"
#include "model/Chords.h"
#include "model/DrumRhythm.h"
#include "model/Scales.h"
#include "model/SliceRhythm.h"
#include "model/SynthEngines.h"
#include <array>
#include <cctype>
#include <numeric>
#include <cmath>
#include <random>
#include <sstream>
#include <string_view>

namespace jambo {

namespace {
using Rng = std::mt19937;
constexpr int perPage = Params::stepsPerPage;

int between(Rng& rng, int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

bool chance(Rng& rng, int percent) {
    return between(rng, 1, 100) <= percent;
}

std::string_view variant(std::string_view table, Rng& rng) {
    int index = between(rng, 0, int(std::count(table.begin(), table.end(), ' ')));
    while (index-- > 0) table.remove_prefix(table.find(' ') + 1);
    return table.substr(0, table.find(' '));
}

std::string_view weighted(std::string_view table, Rng& rng) {
    std::vector<std::string_view> values;
    std::vector<double> weights;
    while (!table.empty()) {
        const auto sep = table.find(':');
        const auto end = table.find(' ');
        values.push_back(table.substr(0, sep));
        weights.push_back(std::stoi(std::string(table.substr(sep + 1, end - sep - 1))));
        if (end == std::string_view::npos) break;
        table.remove_prefix(end + 1);
    }
    return values[std::discrete_distribution<size_t>(weights.begin(), weights.end())(rng)];
}

std::vector<int> progression(int style, const std::vector<int>& avoid, Rng& rng) {
    if (style == 1) return chance(rng, 70) ? std::vector<int>{0} : std::vector<int>{0, between(rng, 5, 6)};
    constexpr const char* progs[] = {
        "1767:30 2511:20 1671:20 1475:15 6545:15", "",
        "1767:35 1765:25 1677:20 1376:20", "1637:50 1767:20 1367:15 1675:15",
        "1564:40 6415:40 1645:20", "14:30 16:30 15:15 64:15 13:10",
    };
    std::vector<int> degrees;
    for (int attempt = 0; attempt < 16; ++attempt) {
        degrees.clear();
        if (chance(rng, 75)) {
            for (char d : weighted(progs[style], rng)) degrees.push_back(d - '1');
        } else {
            degrees.push_back(0);
            while (degrees.size() < (style == 5 ? 2u : 4u)) {
                int next = between(rng, 0, 6);
                if (next != degrees.back()) degrees.push_back(next);
            }
        }
        if (degrees != avoid) break;
    }
    return degrees;
}

PageRange randomPages(const Clip& clip, int visible) {
    if (clip.playFrom >= 0 && clip.playFrom <= clip.playTo && clip.playTo < int(clip.pages.size()))
        return {clip.playFrom, clip.playTo};
    const int page = std::clamp(visible, 0, int(clip.pages.size()) - 1);
    return {page, page};
}

int drumNote(const Track& track, int root) {
    return SynthKey::engine(track.sample) >= 0 ? Scales::baseNote(root, track.keyOctave)
        : Params::sampleRoot + Scales::wrap(root + 6, 12) - 6;
}

std::string lowerName(const Project& p, const Track& t) {
    auto file = p.files.find(t.sample);
    if (file == p.files.end()) return {};
    std::string result = file->second;
    for (char& c : result) {
        const auto ch = static_cast<unsigned char>(c);
        c = std::isalnum(ch) ? char(std::tolower(ch)) : ' ';
    }
    return result;
}

bool contains(std::string_view name, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(), [&](auto word) { return name.find(word) != name.npos; });
}

enum class Part { bass, lead, keys, pad, arp };

Part melodyPart(const Track& t, bool chord, const std::string& name, int overridePart = -1) {
    const int engine = SynthKey::engine(t.sample);
    const bool bass = contains(name, {"bass", "808", "sub", "reese"});
    if (t.arp.mode) return Part::arp;
    if (overridePart == 0) return Part::bass;
    if (overridePart == 1) return chord ? Part::keys : Part::lead;
    if (overridePart == 2) return Part::pad;
    if (engine == J::texture || engine == J::granular ||
        (!bass && contains(name, {"pad", "atmo", "drone", "string", "ambient", "choir", "texture", "riser", "swell"})))
        return Part::pad;
    if (chord) return Part::keys;
    if (engine == J::sub || bass) return Part::bass;
    if (contains(name, {"lead", "pluck", "melody", "arp", "chord", "keys", "piano", "organ", "rhodes"}))
        return Part::lead;
    return t.keyOctave <= 2 ? Part::bass : Part::lead;
}

void arpOrder(std::vector<int>& row, int dir, int octaves, Rng& rng) {
    const auto base = row;
    for (int o = 1; o < octaves; ++o)
        for (int n : base) if (n + o * 12 <= 127) row.push_back(n + o * 12);
    if (dir == Arp::down || dir == Arp::downUp) std::reverse(row.begin(), row.end());
    if (dir == Arp::random) std::shuffle(row.begin(), row.end(), rng);
    if ((dir == Arp::upDown || dir == Arp::downUp) && row.size() > 2) {
        const auto forward = row;
        row.insert(row.end(), forward.rbegin() + 1, forward.rend() - 1);
    }
}

void writeMelody(Page& page, const Track& t, Part part, bool chord, int style, int kind, int scale, int root,
                 int degree, int begin, int end, const std::array<bool, perPage>& kicks, Rng& rng) {
    constexpr const char* bass[] = {
        "26ae 269ae 236abe 267aef", "26ae 236abe 1235679abdef 2367abef 256ade 267aef 12569ade 2356abde", "136be 13bf 157bd 367be", "0a", "02468ace 0368be 0268ae", "0",
    };
    constexpr const char* lead[] = {
        "2369ae 1367ad 256abe 369be", "236ae 269be 136ae", "1367be 3569b 169bd 379ef",
        "0368b 026a 068e 038be", "0248c 02468a 0368c 2468c", "0 08 4c 06c 2a",
    };
    constexpr const char* keys[] = {
        "26ae 25ad 36be 269e 2a", "26ae 269ae 26be", "169e 36bd 159d 37bf 67b",
        "0 08 06a 038", "048c 02468ace 0368be 26ae", "0 08 06 0246",
    };
    const int oct = std::min(t.keyOctave, Scales::topOctave(scale, root, 1, chord, kind));
    const int degreeIndex = Chords::degreeIndex(degree + 1, scale);
    auto harmony = Scales::chord(scale, root, oct, degreeIndex, kind == 6 ? 1 : kind);
    if (style == 1 && kind == 6) harmony.erase(harmony.begin() + 1);
    if (part == Part::arp && t.arp.dir != Arp::order) std::sort(harmony.begin(), harmony.end());
    if (part == Part::arp) arpOrder(harmony, t.arp.dir, t.arp.octaves, rng);
    std::vector<int> hits;
    const int length = end - begin;
    if (part == Part::arp) {
        constexpr int grid[] = {2, 2, 1, 1, 1};
        constexpr const char* cells[] = {
            "", "2369ae 256abe 236abef", "0368be 1369be 0257ad", "", "", "048c 0369c 0268c",
        };
        const auto cell = variant(cells[style], rng);
        for (int i = 0; i < length; i += grid[t.arp.rate]) {
            const char step = i < 10 ? char('0' + i) : char('a' + i - 10);
            if ((cell.empty() || cell.find(step) != cell.npos) && (style != 1 || !kicks[size_t(begin + i)]))
                hits.push_back(i);
        }
        if (hits.empty()) hits.push_back(0);
    } else if (part == Part::pad) {
        hits = {0};
    } else {
        const auto cells = variant(part == Part::bass ? bass[style] : part == Part::keys ? keys[style] : lead[style], rng);
        for (char c : cells) {
            const int s = c <= '9' ? c - '0' : c - 'a' + 10;
            if (s < length) hits.push_back(s);
        }
        if (part == Part::bass && style == 3) {
            hits.clear();
            for (int i = 0; i < length; ++i)
                if (kicks[size_t(begin + i)]) hits.push_back(i);
        } else if (style <= 2 || part == Part::bass) {
            hits.erase(std::remove_if(hits.begin(), hits.end(), [&](int i) {
                return kicks[size_t(begin + i)];
            }), hits.end());
        }
        if (hits.empty() && (part != Part::bass || style != 3)) {
            for (int i = 0; i < length; ++i) {
                if (kicks[size_t(begin + i)]) continue;
                hits.push_back(i);
                break;
            }
        }
    }
    for (size_t i = 0; i < hits.size(); ++i) {
        const int s = begin + hits[i];
        Step& step = page[size_t(s)];
        step.on = true;
        if (part == Part::keys || (part == Part::pad && chord)) step.notes = harmony;
        else {
            int n = harmony.front();
            if (part == Part::arp) n = harmony[i % harmony.size()];
            else if (part == Part::lead || (part == Part::bass && i > 0 && chance(rng, 30)))
                n = harmony[size_t(between(rng, 0, int(harmony.size()) - 1))];
            step.notes = {n};
        }
        const int arpLen = int(std::lround(t.arp.gate * Arp::rateSteps[t.arp.rate]));
        step.setAllLen(part == Part::pad ? length : part == Part::arp ? std::max(1, arpLen) : 1);
        step.locks["level"] = i == 0 && part != Part::pad ? 127 : between(rng, 76, 108);
    }
}

}  // namespace

void App::setRandomSettings(int options) {
    randomSettings_ = options & (randomChords | randomDrums | randomSamples | randomSwing | randomAll | showRandomPart);
    bump();
}

int App::randomDrumRole(const Track& t) const {
    const int engineRole = t.drumRole();
    if (engineRole >= 0 || t.sample.empty() || SynthKey::engine(t.sample) >= 0 || t.hasCuts()) return engineRole;
    const auto name = lowerName(project_, t);
    if (contains(name, {"kick", "bassdrum"})) return DrumRhythm::kick;
    if (contains(name, {"snare", "rimshot"})) return DrumRhythm::snare;
    if (contains(name, {"clap"})) return DrumRhythm::clap;
    if (contains(name, {"hat", "ride"})) return DrumRhythm::hat;
    if (contains(name, {"perc", "tom", "conga", "shaker", "bongo", "tamb", "cowbell", "clave"})) return DrumRhythm::perc;
    std::istringstream words(name);
    for (std::string word; words >> word;) {
        if (word == "bd" || word == "kik") return DrumRhythm::kick;
        if (word == "sd" || word == "snr" || word == "rim") return DrumRhythm::snare;
        if (word == "cp" || word == "clp") return DrumRhythm::clap;
        if (word == "hh" || word == "ch" || word == "oh") return DrumRhythm::hat;
    }
    return -1;
}

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

int App::randomPart() const {
    if (channel_ != Channel::track || arp().mode || chordStyle() >= 0 || !(randomSettings_ & showRandomPart)) return -1;
    const auto found = randomParts_.find(track().id);
    const auto part = melodyPart(track(), chordOn(), lowerName(project_, track()),
                                 found == randomParts_.end() ? -1 : found->second);
    return part == Part::bass ? 0 : part == Part::pad ? 2 : 1;
}

void App::chooseRandomPart(int part) {
    if (randomPart() < 0 || part < 0 || part > 2) return;
    const auto found = randomParts_.find(track().id);
    if (found != randomParts_.end() && found->second == part) randomParts_.erase(found);
    else randomParts_[track().id] = part;
    bump();
}

void App::writeRandom() {
    static Rng rng(std::random_device{}());
    const bool all = (randomSettings_ & randomAll) != 0;
    const int style = DrumRhythm::styleIndex(track().drumStyle);
    const auto degrees = progression(style, lastProgression_, rng);
    constexpr const char* kinds[] = {
        "1:60 2:40", "1:1 2:1 6:1", "1:50 0:30 2:20", "0:50 1:30 3:20", "0:50 5:30 3:10 4:10", "3:40 5:35 2:25",
    };
    const int kind = weighted(kinds[style], rng).front() - '0';
    edit([&](Project& p) {
        std::vector<size_t> order(p.tracks.size());
        std::iota(order.begin(), order.end(), size_t(0));
        if (all) std::stable_partition(order.begin(), order.end(), [&](size_t i) {
            return randomDrumRole(p.tracks[i]) >= 0 || randomSlices(p.tracks[i]);
        });
        for (size_t i : order) {
            const bool selected = i == size_t(selected_);
            Track& t = p.tracks[i];
            if ((!selected && !all) || (all && t.sample.empty())) continue;
            const int drum = randomDrumRole(t);
            const bool slices = randomSlices(t);
            if (drum >= 0 && !(randomSettings_ & randomDrums)) continue;
            if (drum < 0 && !slices && !(randomSettings_ & randomChords)) continue;
            const auto data = slices && samples_ ? samples_->data(t.sample) : nullptr;
            if (slices && !data) continue;
            const auto pieces = data ? SliceRhythm::slicePieces(t, *data, p) : std::vector<SliceRhythm::SlicePiece>{};
            if (slices && pieces.empty()) continue;
            const auto range = randomPages(t.openClip(), selected ? viewPage() : 0);
            std::vector<int> lengths;
            for (int page = range.lower; page <= range.upper; ++page)
                lengths.push_back(t.openClip().pageLen(page, range.lower));
            const int trackStyle = all ? style : DrumRhythm::styleIndex(t.drumStyle);
            const int note = drumNote(t, p.root);
            const bool chord = chordMode_[i] && !t.arp.mode;
            const auto overridePart = randomParts_.find(t.id);
            const Part part = melodyPart(t, chord, lowerName(p, t),
                (randomSettings_ & showRandomPart) && overridePart != randomParts_.end() ? overridePart->second : -1);
            const bool technoProgression = lastProgression_ == std::vector<int>{0} ||
                lastProgression_ == std::vector<int>{0, 5} || lastProgression_ == std::vector<int>{0, 6};
            const bool reuse = !all && (part == Part::bass || part == Part::lead) &&
                !lastProgression_.empty() && (trackStyle != 1 || technoProgression);
            const auto harmonyDegrees = reuse ? lastProgression_ : degrees;
            std::vector<int> base;
            if (drum >= 0) {
                auto& last = lastDrumPattern_[t.id];
                for (int tries = 0; tries < 16; ++tries) {
                    base = DrumRhythm::drumBase(drum, trackStyle, lengths.front(), rng);
                    if (base != last) break;
                }
                last = base;
            } else if (!slices) lastProgression_ = harmonyDegrees;
            auto& pages = t.pagesMut();
            const int activePages = int(std::count_if(lengths.begin(), lengths.end(), [](int len) { return len > 0; }));
            for (size_t j = 0; j < lengths.size(); ++j) {
                const int len = lengths[j];
                Page& page = pages[size_t(range.lower) + j];
                page = emptyPage();
                if (len == 0) continue;
                if (drum >= 0) {
                    DrumRhythm::writeDrums(page, base, drum, trackStyle, note, len, j > 0 && j + 1 < size_t(activePages),
                               j > 0 && j + 1 == size_t(activePages), rng);
                } else if (slices) {
                    auto* last = j == 0 ? &lastDrumPattern_[-1 - t.id] : nullptr;
                    SliceRhythm::writePage(page, pieces, t, note, len, trackStyle, last, rng);
                } else {
                    std::array<bool, perPage> kicks{};
                    bool hasKickTrack = false;
                    for (const auto& kick : p.tracks) {
                        if (randomDrumRole(kick) != DrumRhythm::kick) continue;
                        const auto& clip = kick.openClip();
                        const auto& pages = clip.pages;
                        if (std::none_of(pages.begin(), pages.end(), [](const Page& pg) {
                            return std::any_of(pg.begin(), pg.end(), [](const Step& step) { return step.on; });
                        })) continue;
                        const int count = std::clamp((clip.length + perPage - 1) / perPage, 1, int(pages.size()));
                        const int base = (range.lower + int(j)) % count * perPage;
                        for (int step = 0; step < perPage && base + step < clip.length; ++step) {
                            const Step* hit = clip.step(base + step);
                            if (hit && hit->on) {
                                hasKickTrack = true;
                                kicks[size_t(step)] = true;
                            }
                        }
                        break;
                    }
                    if (!hasKickTrack) {
                        constexpr std::string_view fallback[] = {"048c", "048c", "02a", "0a", "08", ""};
                        for (char k : fallback[trackStyle]) kicks[size_t(k <= '9' ? k - '0' : k - 'a' + 10)] = true;
                    }
                    size_t harmony = j;
                    if (trackStyle == 1) harmony = j * harmonyDegrees.size() / size_t(activePages);
                    if (trackStyle == 5) harmony = size_t(j >= size_t((activePages + 1) / 2));
                    const bool split = activePages == 1 && len > 8 && trackStyle != 1 && trackStyle != 3 && trackStyle != 5;
                    writeMelody(page, t, part, chord, trackStyle, kind, p.scale, p.root,
                                harmonyDegrees[harmony % harmonyDegrees.size()], 0, split ? 8 : len, kicks, rng);
                    if (split) writeMelody(page, t, part, chord, trackStyle, kind, p.scale, p.root,
                                           harmonyDegrees[1 % harmonyDegrees.size()], 8, len, kicks, rng);
                }
            }
            if ((randomSettings_ & randomSwing) && (drum >= 0 || slices || (part != Part::pad && part != Part::arp)))
                t.params["swing"] = DrumRhythm::swing[trackStyle];
        }
    });
}

}  // namespace jambo
