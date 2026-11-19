// Порт Core/Model/ProjectRepair.swift (ветка swift JAMbo): проект после чтения — миграции версий, размеры массивов и
// номера в пределах, которые ждут секвенсор и экран; новые параметры — значения по умолчанию.
#include <cmath>
#include "DrumRhythm.h"
#include "Project.h"

namespace jambo {

/// Значения по умолчанию, поверх — свои (merging { _, v in v }).
static ParamMap merged(ParamMap def, const ParamMap& own) {
    for (const auto& kv : own) def[kv.first] = kv.second;
    return def;
}

void Project::repair() {
    if (version < 3) migrateSwing();
    if (version < 4) migrateSynthKnobs();
    if (version < 6) migrateGranular();
    if (version < 7) migrateRandomStyles();
    if (version < 8) migrateMidiIn();
    if (version < 9) migrateFilter();
    if (version < 10) migrateTrackFx();
    version = std::max(version, 10);
    if (!std::isfinite(bpm)) bpm = 120;
    bpm = std::min(Params::bpmMax, std::max(Params::bpmMin, bpm));
    scale = clamp(scale, 0, int(Scales::all.size()) - 1);
    root = clamp(root, 0, 11);
    fx = merged(Params::fxDefaults(), fx);
    master = merged(MasterChain::defaultValues(), master);
    if (tracks.size() > size_t(Params::maxTracks)) tracks.resize(size_t(Params::maxTracks));
    while (tracks.size() < size_t(Params::trackCount)) tracks.emplace_back(int(tracks.size()));
    const int n = int(tracks.size());
    for (auto& t : tracks) t.repair(n);
    static const char* const names[2] = {"MIX", "MST"};
    if (chanSeqs.size() > 2) chanSeqs.resize(2);
    while (chanSeqs.size() < 2) chanSeqs.push_back(Track::channel(names[chanSeqs.size()]));
    for (size_t k = 0; k < 2; ++k) {
        chanSeqs[k].name = names[k];
        chanSeqs[k].repair(n);
    }
    masterEq.repair(n);
    scenes.resize(size_t(Params::patternSlots));
    for (auto& s : scenes) s.repeats = std::max(1, s.repeats);
    rows = clamp(rows, 1, Params::patternSlots);
    arrStart = clamp(arrStart, 0, Params::patternSlots - 1);
    for (auto& t : tracks) t.repairBlocks();
    arrange.repair();
}

void Project::migrateSwing() {
    auto strip = [](Track& t) {
        t.assigns.erase("swing");
        if (t.home) t.home->assigns.erase("swing");
        for (auto& c : t.clips) {
            if (!c) continue;
            if (c->src) c->src->assigns.erase("swing");
            for (auto& page : c->pages)
                for (auto& s : page) {
                    s.locks.erase("swing");
                    if (s.soundLocks) {
                        auto& sl = *s.soundLocks;
                        sl.erase(std::remove(sl.begin(), sl.end(), "swing"), sl.end());
                    }
                }
        }
    };
    for (auto& t : tracks) strip(t);
    for (auto& t : chanSeqs) strip(t);
}

std::set<std::string> Project::sampleKeys() const {
    std::set<std::string> keys;
    auto addSound = [&keys](const TrackSound& snd) {
        keys.insert(snd.sample);
        keys.insert(snd.grain.value_or(""));
        for (const auto& k : snd.folder.sampleKeys()) keys.insert(k);
        for (const auto& kv : snd.slices) keys.insert(kv.first);
    };
    for (const auto& t : tracks) {
        keys.insert(t.sample);
        for (const auto& k : t.folder.sampleKeys()) keys.insert(k);
        for (const auto& kv : t.slices) keys.insert(kv.first);
        keys.insert(t.grain);
        for (const auto& it : t.folder.items)
            if (it.preset && it.preset->grain) keys.insert(*it.preset->grain);
        if (t.home) addSound(*t.home);
        for (const auto& c : t.clips) if (c && c->src) addSound(*c->src);
        for (const auto& c : t.clips) {
            if (!c) continue;
            for (const auto& page : c->pages)
                for (const auto& s : page) if (s.sound) keys.insert(*s.sound);
        }
    }
    if (arrange.ref) keys.insert(arrange.ref->key);
    std::set<std::string> out;
    for (const auto& k : keys) if (!k.empty() && k.find(':') == std::string::npos) out.insert(k);
    return out;
}

void Track::repair(int n) {
    params = merged(Params::defaults(), params);
    clips.resize(size_t(Params::patternSlots));
    for (auto& c : clips) if (c) c->repair();
    slot = Project::clamp(slot, 0, Params::patternSlots - 1);
    open = Project::clamp(open, 0, Params::patternSlots - 1);
    folder.index = Project::clamp(folder.index, -1, int(folder.items.size()) - 1);
    if (!std::isfinite(vol)) vol = Params::volDef;
    trim = std::isfinite(trim) ? std::min(0.0, std::max(Params::volMin, trim)) : 0;
    eq.repair(n);
}

void Clip::repair() {
    if (pages.empty()) pages = {emptyPage()};
    if (pages.size() > size_t(Params::maxPages)) pages.resize(size_t(Params::maxPages));
    const int n = Params::stepsPerPage;
    for (auto& page : pages) {
        page.resize(size_t(n));
        for (auto& s : page) s.repair();
    }
    length = Project::clamp(length, 1, int(pages.size()) * n);
    playTo = Project::clamp(playTo, 0, int(pages.size()) - 1);
    playFrom = Project::clamp(playFrom, 0, playTo);
    if (scale) scale = Project::clamp(*scale, 0, int(Scales::all.size()) - 1);
    if (root) root = Project::clamp(*root, 0, 11);
    if (rate) {
        if (*rate == 0) rate.reset(); else rate = Project::clamp(*rate, -4, 1);
    }
}

void Step::repair() {
    len = std::max(1, len);
    if (lens) {
        if (lens->size() == notes.size() && !lens->empty()) {
            for (int& l : *lens) l = std::max(1, l);
            len = *std::max_element(lens->begin(), lens->end());
        } else {
            lens.reset();
        }
    }
    if (vels && (vels->size() != notes.size() || vels->empty())) vels.reset();
}

void Project::migrateMidiIn() {
    for (auto& t : tracks)
        if (t.midi.inOn && !t.midi.inDevice && t.midi.inChannel == 0) t.midi.inOn = false;
}

void Project::migrateRandomStyles() {
    if (tracks.empty()) return;
    size_t src = 0;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].drumRole() >= 0 || tracks[i].hasCuts()) {
            src = i;
            break;
        }
    const int v = DrumRhythm::migrateStyle(tracks[src].drumStyle);
    for (auto& t : tracks) t.drumStyle = v;
}

}  // namespace jambo
