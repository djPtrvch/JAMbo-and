// Случайные мелодические партии по правилам Swift JAMbo.
#pragma once
#include "Arp.h"
#include "RhythmRandom.h"
#include <optional>
#include <set>
#include <string>

namespace jambo {

struct RhythmNote {
    int step, note, length;
    double velocity;
};
using RhythmPattern = std::vector<std::vector<RhythmNote>>;

namespace MelodyRhythm {
using RhythmRandom::Ints;
using RhythmRandom::Rng;
using Kicks = std::vector<std::set<int>>;
enum class Role { bass, lead, keys, pad, arp };

struct Part {
    Role role;
    int style, kind, scale, root, octave;
    bool single;
    TrackArp arp;
};

std::string words(const std::string& name);
int drumRoleByName(const std::string& name);
Role role(int engine, const std::string& name, int octave, bool chord, bool arp, int overridePart);
Ints progression(int style, int scale, const Ints* avoid, Rng& rng);
bool isTechnoProgression(const Ints& degrees);
int kind(int style, Rng& rng);
std::set<int> defaultKick(int style);
RhythmPattern generate(const Part& part, const Ints& lengths, const Ints& degrees, const Kicks& kicks, Rng& rng);

namespace Detail {
constexpr int per = 16;
int note(int degree, const Part& part);
Ints closeVoicing(int root, const Ints& offsets, int n, std::optional<int> previousTop);
Ints arpSequence(Ints notes, int dir, int octaves, int span);
Ints playingPages(const Ints& lengths);
RhythmPattern techno(const Part& part, const Ints& lengths, const Ints& degrees, const Kicks& kicks, Rng& rng);
RhythmPattern styled(const Part& part, const Ints& lengths, const Ints& degrees, const Kicks& kicks, Rng& rng);
}
}
}
