// Роли, гармония и общие правила мелодических генераторов.
#include "MelodyRhythm.h"
#include "DrumRhythm.h"
#include "Scales.h"
#include <cctype>
#include <climits>
#include <cstdlib>

namespace jambo::MelodyRhythm {
using namespace RhythmRandom;

namespace {
bool has(std::string_view name, std::initializer_list<std::string_view> keys) {
    return std::any_of(keys.begin(), keys.end(), [&](auto key) { return name.find(key) != name.npos; });
}

std::string_view weightedRow(std::string_view table, Rng& rng) {
    const auto rows = split(table);
    std::vector<double> weights;
    for (auto row : rows) weights.push_back(std::atoi(std::string(row.substr(row.find(':') + 1)).c_str()));
    const auto row = rows[weighted(rng, weights)];
    return row.substr(0, row.find(':'));
}
}

std::string words(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 128) c = std::isalnum(byte) ? char(std::tolower(byte)) : ' ';
    }
    return result;
}

int drumRoleByName(const std::string& name) {
    const auto text = words(name);
    const auto tokens = split(text);
    std::string joined;
    for (auto token : tokens) joined += token;
    const auto any = [&](std::initializer_list<std::string_view> exact, std::initializer_list<std::string_view> prefixes) {
        for (auto token : tokens) {
            if (std::find(exact.begin(), exact.end(), token) != exact.end()) return true;
            for (auto prefix : prefixes) if (token.substr(0, prefix.size()) == prefix) return true;
        }
        return false;
    };
    if (has(joined, {"kick", "bassdrum"}) || any({"bd", "kik"}, {"kick", "bassdrum"})) return DrumRhythm::kick;
    if (has(joined, {"snare"}) || any({"sd", "snr", "rim", "rimshot"}, {"snare"})) return DrumRhythm::snare;
    if (has(joined, {"clap"}) || any({"cp", "clp"}, {"clap"})) return DrumRhythm::clap;
    if (has(joined, {"hihat"}) || any({"hh", "oh", "ch", "ride"}, {"hat"})) return DrumRhythm::hat;
    if (any({"perc"}, {"perc", "tom", "conga", "bongo", "shaker", "tamb", "cowbell", "clave"})) return DrumRhythm::perc;
    return -1;
}

Role role(int engine, const std::string& name, int octave, bool chord, bool arp, int overridePart) {
    if (arp) return Role::arp;
    if (overridePart == 0) return Role::bass;
    if (overridePart == 1) return chord ? Role::keys : Role::lead;
    if (overridePart == 2) return Role::pad;
    const auto text = words(name);
    const bool bass = has(text, {"bass", "808", "sub", "reese"});
    if (engine == J::texture || engine == J::granular ||
        (engine < 0 && !bass && has(text, {"pad", "atmo", "drone", "string", "ambient", "choir", "texture", "fx",
                                           "noise", "riser", "sweep", "soundscape", "swell"}))) return Role::pad;
    if (chord) return Role::keys;
    if (engine == J::sub || (engine < 0 && bass)) return Role::bass;
    if (engine < 0 && has(text, {"lead", "pluck", "melody", "arp", "chord", "keys", "piano", "stab", "organ",
                                  "rhodes", "epiano", "clav", "wurli"})) return Role::lead;
    return octave <= 2 ? Role::bass : Role::lead;
}

Ints progression(int style, int scale, const Ints* avoid, Rng& rng) {
    if (style == 1) return real(rng) < 0.7 ? Ints{1} : Ints{1, coin(rng) ? 7 : 6};
    constexpr const char* rows[] = {
        "1767:30 2511:20 1671:20 1475:15 6545:15", "",
        "1767:35 1765:25 1677:20 1376:20", "1637:50 1767:20 1367:15 1675:15",
        "1564:40 6415:40 1645:20", "14:30 16:30 15:15 64:15 13:10",
    };
    Ints out;
    for (int i = 0; i < 16; ++i) {
        if (real(rng) < 0.75) out = digits(weightedRow(rows[style], rng));
        else {
            const int top = std::clamp(int(Scales::all[size_t(scale)].steps.size()), 2, 7);
            out = {1};
            while (out.size() < 4) {
                const int d = integer(rng, 1, top);
                if (d != out.back()) out.push_back(d);
            }
            if (style == 5) out.resize(2);
        }
        if (!avoid || out != *avoid) break;
    }
    return out;
}

bool isTechnoProgression(const Ints& d) {
    return d == Ints{1} || d == Ints{1, 6} || d == Ints{1, 7};
}

int kind(int style, Rng& rng) {
    if (style == 1) return integer(rng, 0, 2);
    constexpr const char* rows[] = {
        "1:60 2:40", "", "1:50 0:30 2:20", "0:50 1:30 3:20", "0:50 5:30 3:10 4:10", "3:40 5:35 2:25",
    };
    return hexDigit(weightedRow(rows[style], rng).front());
}

std::set<int> defaultKick(int style) {
    constexpr const char* steps[] = {"048c", "048c", "02a", "0a", "08", ""};
    const auto hits = digits(steps[style]);
    return {hits.begin(), hits.end()};
}

RhythmPattern generate(const Part& part, const Ints& lengths, const Ints& degrees, const Kicks& kicks, Rng& rng) {
    return part.style == 1 ? Detail::techno(part, lengths, degrees, kicks, rng)
                           : Detail::styled(part, lengths, degrees, kicks, rng);
}

namespace Detail {
int note(int degree, const Part& part) {
    int n = Scales::note(part.scale, part.root, part.octave, degree);
    while (n > 127) n -= 12;
    return std::max(0, n);
}

Ints closeVoicing(int root, const Ints& offsets, int n, std::optional<int> previousTop) {
    Ints folded;
    for (int off : offsets) folded.push_back(Scales::wrap(root + off, n));
    std::sort(folded.begin(), folded.end());
    if (!previousTop || folded.empty()) return folded;
    Ints best = folded;
    int distance = INT_MAX;
    for (int shift : {0, -1, 1}) {
        for (size_t inv = 0; inv < folded.size(); ++inv) {
            auto row = folded;
            for (size_t i = 0; i < row.size(); ++i) row[i] += (shift + int(i < inv)) * n;
            const int d = std::abs(*std::max_element(row.begin(), row.end()) - *previousTop);
            if (d >= distance) continue;
            distance = d;
            std::sort(row.begin(), row.end());
            best = std::move(row);
        }
    }
    return best;
}

Ints arpSequence(Ints notes, int dir, int octaves, int span) {
    Ints base;
    for (int n : notes) if (std::find(base.begin(), base.end(), n) == base.end()) base.push_back(n);
    if (dir != Arp::order) std::sort(base.begin(), base.end());
    Ints row;
    for (int i = 0; i < Arp::clampOctaves(octaves); ++i)
        for (int n : base) if (n + i * span <= 127) row.push_back(n + i * span);
    if (dir == Arp::down || dir == Arp::downUp) std::reverse(row.begin(), row.end());
    if ((dir == Arp::upDown || dir == Arp::downUp) && row.size() > 2) {
        const Ints forward = row;
        row.insert(row.end(), forward.rbegin() + 1, forward.rend() - 1);
    }
    return row;
}

Ints playingPages(const Ints& lengths) {
    Ints pages;
    for (size_t i = 0; i < lengths.size(); ++i) if (lengths[i] > 0) pages.push_back(int(i));
    return pages;
}
}
}
