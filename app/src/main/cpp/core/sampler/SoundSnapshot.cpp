// Порт Core/Sampler/SoundSnapshot.swift (ветка swift JAMbo).
#include "SoundSnapshot.h"
#include <set>
#include "eq/EqSnapshot.h"
#include "model/Project.h"

namespace jambo {

int SoundSnapshot::liveCount = 0;

int SoundData::entry(int t, uint64_t hash) const {
    if (hash == 0 || t < 0 || t >= trackCount) return -1;
    const int u = s(t);
    const int a = entryStart[size_t(u)];
    const int n = entryCount[size_t(u)];
    for (int k = 0; k < n; ++k)
        if (eHash[size_t(a + k)] == hash) return a + k;
    return -1;
}

int SoundData::anyEntry(uint64_t hash) const {
    for (int t = 0; t < trackCount; ++t) {
        const int e = entry(t, hash);
        if (e >= 0) return e;
    }
    for (int k = extraStart; k < extraStart + extraCount; ++k)
        if (eHash[size_t(k)] == hash) return k;
    return -1;
}

bool SoundData::holds(const float* data) const {
    for (int k = 0; k < entryTotal; ++k)
        if (eLeft[size_t(k)] == data) return true;
    return false;
}

namespace {
double paramOr(const ParamMap& m, const std::string& id) {
    auto it = m.find(id);
    if (it != m.end()) return it->second;
    const ParamDef* d = Params::byId(id);
    return d ? d->def : 0;
}
double paramOr0(const ParamMap& m, const std::string& id) {
    auto it = m.find(id);
    return it != m.end() ? it->second : 0;
}
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
}  // namespace

SoundSnapshot::SoundSnapshot(const Project& project, const std::map<std::string, SampleRef>& samples,
                             const Options& o) {
    const int tracks = SoundData::maxTracks;
    const int tracks2 = tracks * 2;
    const int entries = SoundData::maxEntries;
    const int slices = SoundData::maxSlices;
    const int pc = VoiceParam::count;
    SoundData& d = data_;
    if (!o.slots) ownSlots.assign(size_t(tracks), 0);
    d.slotRef = o.slots ? o.slots : ownSlots.data();
    d.rebind = o.rebind;
    d.stretchAlgo = o.stretchBeat ? 2 : 1;
    d.master.assign(size_t(MasterChain::Index::count), 0);
    MasterChain::fill(d.master.data(), project);
    if (int(o.master.size()) == MasterChain::Index::count) d.master = o.master;
    // Два звука дорожки: активного клипа и следующего (next).
    const int n0 = std::min(int(project.tracks.size()), tracks);
    d.alt.assign(size_t(tracks), 0);
    d.altSlot.assign(size_t(tracks), -1);
    std::vector<int> slotOf(static_cast<size_t>(tracks2), 0);
    std::vector<std::optional<TrackSound>> sounds(static_cast<size_t>(tracks2));
    std::map<int, int> chosen;
    for (int t = 0; t < n0; ++t) {
        const Track& tr = project.tracks[size_t(t)];
        const int a = tr.slot;
        const int q0 = t < int(o.next.size()) ? o.next[size_t(t)] : -1;
        const int q = q0 != a && tr.clipFilled(q0) ? q0 : -1;
        const std::optional<TrackSound> nextSound = q >= 0 ? std::optional<TrackSound>(tr.soundSlot(q)) : std::nullopt;
        auto ka = o.keepAlt.find(tr.id);
        if (ka != o.keepAlt.end() && ka->second == a) {
            // Активный клип уже на втором звуке: там и остаётся.
            d.alt[size_t(t)] = 1;
            d.altSlot[size_t(t)] = a;
            sounds[size_t(tracks + t)] = tr.sound();
            slotOf[size_t(tracks + t)] = a;
            sounds[size_t(t)] = nextSound ? *nextSound : (tr.home ? *tr.home : tr.sound());
            slotOf[size_t(t)] = q >= 0 ? q : -1;
            chosen[tr.id] = a;
        } else {
            sounds[size_t(t)] = tr.sound();
            slotOf[size_t(t)] = a;
            if (nextSound && *nextSound != tr.sound()) {
                d.alt[size_t(t)] = 1;
                d.altSlot[size_t(t)] = q;
                sounds[size_t(tracks + t)] = *nextSound;
                slotOf[size_t(tracks + t)] = q;
                chosen[tr.id] = q;
            }
        }
    }
    d.eq.assign(size_t(EqSpec::channels + tracks) * EqSpec::stride, 0);
    for (int ch = 0; ch < EqSpec::channels + tracks; ++ch) {
        const Eq* eq = nullptr;
        if (ch == EqSpec::masterChannel) {
            eq = &project.masterEq;
        } else if (ch < tracks) {
            if (sounds[size_t(ch)]) eq = &sounds[size_t(ch)]->eq;
        } else if (ch >= EqSpec::channels) {
            if (sounds[size_t(tracks + ch - EqSpec::channels)]) eq = &sounds[size_t(tracks + ch - EqSpec::channels)]->eq;
        }
        EqSpec::fill(d.eq.data() + size_t(ch) * EqSpec::stride, eq);
    }
    d.mod.assign(size_t(tracks2 * Params::modCount), 0);
    d.lfoSync.assign(size_t(tracks2 * 3), 0);
    d.rateMode.assign(size_t(tracks2), 0);
    d.pitchKey.assign(size_t(tracks2), 0);
    d.asSrc.assign(size_t(tracks2 * ModTarget::count), 0);
    d.asAmt.assign(size_t(tracks2 * ModTarget::count), 0);
    d.scale.assign(size_t(tracks2 * ModTarget::count), ParamScale());
    d.asAny.assign(size_t(tracks2), 0);
    const auto& fxIds = Params::fxIds;
    d.fxSrc.assign(size_t(Params::fxCount), 0);
    d.fxAmt.assign(size_t(Params::fxCount), 0);
    d.fxTrack.assign(size_t(Params::fxCount), 0);
    d.fxScale.assign(size_t(Params::fxCount), ParamScale());
    d.fx.assign(size_t(Params::fxCount), 0);
    for (int k = 0; k < int(fxIds.size()) && k < Params::fxCount; ++k) {
        const std::string& id = fxIds[size_t(k)];
        auto a = project.fxAssigns.find(id);
        if (a != project.fxAssigns.end()) {
            d.fxSrc[size_t(k)] = int8_t(a->second.lfo);
            d.fxAmt[size_t(k)] = float(a->second.amount);
            d.fxTrack[size_t(k)] = int32_t(a->second.track.value_or(0));
        }
        if (const ParamDef* p = Params::byId(id)) d.fxScale[size_t(k)] = ParamScale(*p);
        d.fx[size_t(k)] = float(paramOr(project.fx, id));
    }
    fillClipFx(project, tracks);
    d.scAmount.assign(size_t(tracks2), 0);
    d.scSend = {o.scSends.rev ? 1.f : 0.f, o.scSends.dly ? 1.f : 0.f, o.scSends.drv ? 1.f : 0.f};
    d.tableRef.assign(size_t(tracks2), WtRef());
    d.grainRef.assign(size_t(tracks2), GrainSrc());
    d.trackId.assign(size_t(tracks), -1);
    d.params.assign(size_t(tracks2 * pc), 0);
    d.gain.assign(size_t(tracks2), 0);
    d.trim.assign(size_t(tracks), 1);
    d.open.assign(size_t(tracks), 0);
    d.midiOut.assign(size_t(tracks), 0);
    d.sound.assign(size_t(tracks2), 0);
    d.entryStart.assign(size_t(tracks2), 0);
    d.entryCount.assign(size_t(tracks2), 0);
    d.eHash.assign(size_t(entries), 0);
    d.eLeft.assign(size_t(entries), nullptr);
    d.eRight.assign(size_t(entries), nullptr);
    d.eFrames.assign(size_t(entries), 0);
    d.eChannels.assign(size_t(entries), 1);
    d.eSliceStart.assign(size_t(entries), 0);
    d.eSliceCount.assign(size_t(entries), 0);
    d.eBpm.assign(size_t(entries), 0);
    d.eOnsets.assign(size_t(entries), nullptr);
    d.eOnsetCount.assign(size_t(entries), 0);
    d.sliceAt.assign(size_t(slices), 0);
    d.sliceNote.assign(size_t(slices), 0);
    int e = 0, sl = 0;
    d.trackCount = std::min(int(project.tracks.size()), tracks);
    const std::vector<int> order = EqSpec::mixOrder(project, d.trackCount);
    d.eqOrder.assign(size_t(tracks), 0);
    for (int t = 0; t < tracks; ++t) d.eqOrder[size_t(t)] = t < int(order.size()) ? order[size_t(t)] : t;
    d.eqDep.assign(size_t(tracks), 0);
    for (int t = 0; t < d.trackCount; ++t)
        if (EqSpec::listensToTrack(project, t, d.trackCount)) d.eqDep[size_t(t)] = 1;
    for (int t = 0; t < d.trackCount; ++t) {
        const Track& tr = project.tracks[size_t(t)];
        d.trackId[size_t(t)] = tr.id;
        // LEVEL ABSOLUTE — громкость ровно как в MIX: подстройка не действует.
        const float tg = o.levelRelative ? float(Params::volGain(tr.trim)) : 1;
        d.trim[size_t(t)] = tg;
        d.gain[size_t(t)] = float(Params::volGain(tr.volSlot(slotOf[size_t(t)]))) * tg;
        d.gain[size_t(tracks + t)] = float(Params::volGain(tr.volSlot(slotOf[size_t(tracks + t)]))) * tg;
        d.open[size_t(t)] = project.audible(t) ? 1 : 0;
        d.midiOut[size_t(t)] = tr.midi.sends() ? 1 : 0;
    }
    // Сэмплы — свой звук шагов дорожек (во всех клипах).
    std::vector<std::vector<std::string>> stepSounds(static_cast<size_t>(tracks));
    for (int t = 0; t < d.trackCount; ++t) {
        std::set<std::string> seen;
        for (const auto& c : project.tracks[size_t(t)].clips) {
            if (!c) continue;
            for (const Page& page : c->pages)
                for (const Step& st : page) {
                    if (!st.sound) continue;
                    const std::string& k = *st.sound;
                    if (!k.empty() && k.find(':') == std::string::npos && !seen.count(k)) {
                        seen.insert(k);
                        stepSounds[size_t(t)].push_back(k);
                    }
                }
        }
    }
    const auto& vids = VoiceParam::ids();
    const auto& mids = Params::modIds();
    const auto& tids = ModTarget::ids();
    for (int u = 0; u < tracks2; ++u) {
        const int t = u % tracks;
        if (t >= d.trackCount || !sounds[size_t(u)]) continue;
        if (u >= tracks && d.alt[size_t(t)] == 0) continue;
        const TrackSound& snd = *sounds[size_t(u)];
        for (int p = 0; p < pc; ++p) d.params[size_t(u * pc + p)] = float(paramOr(snd.params, vids[size_t(p)]));
        d.scAmount[size_t(u)] = float(paramOr0(snd.params, "sidechain"));
        for (int k = 0; k < int(mids.size()); ++k)
            d.mod[size_t(u * Params::modCount + k)] = float(paramOr(snd.params, mids[size_t(k)]));
        for (int l = 0; l < int(Params::lfoSyncIds.size()); ++l)
            d.lfoSync[size_t(u * 3 + l)] = paramOr0(snd.params, Params::lfoSyncIds[size_t(l)]) >= 0.5 ? 1 : 0;
        const bool tstr = paramOr0(snd.params, Params::rateModeId) >= 0.5;
        d.rateMode[size_t(u)] = tstr ? (o.stretchBeat ? 2 : 1) : 0;
        d.pitchKey[size_t(u)] = paramOr0(snd.params, Params::stepModeId("pitch")) >= 0.5 ? 1 : 0;
        // Шкалы целей — по сетке звука дорожки.
        std::map<std::string, const ParamDef*> grid;
        for (const auto& p : Params::grid(snd.sample))
            if (!p.id.empty()) grid[p.id] = &p;
        for (int j = 0; j < ModTarget::count; ++j) {
            const size_t i = size_t(u * ModTarget::count + j);
            const std::string& id = tids[size_t(j)];
            auto g = grid.find(id);
            const ParamDef* def = g != grid.end() ? g->second : Params::byId(id);
            if (def) d.scale[i] = ParamScale(*def);
            auto a = snd.assigns.find(id);
            if (a != snd.assigns.end()) {
                d.asSrc[i] = int8_t(a->second.lfo);
                d.asAmt[i] = float(a->second.amount);
                d.asAny[size_t(u)] = 1;
            }
        }
        d.sound[size_t(u)] = SoundKey::hash(snd.sample);
        // Гамма и нота нарезки — клипа, который играет этот звук.
        const std::pair<int, int> clipKey = project.clipKey(t, slotOf[size_t(u)]);
        auto wt = o.tables.find(snd.table);
        if (wt != o.tables.end() && wt->second) {
            keepTables.push_back(wt->second);
            d.tableRef[size_t(u)] = wt->second->ref();
        }
        if (snd.grain) {
            auto gs = samples.find(*snd.grain);
            if (gs != samples.end() && gs->second) {
                const SampleData& s = *gs->second;
                keep.push_back(gs->second);
                d.grainRef[size_t(u)] = GrainSrc{s.left(), s.channels() > 1 ? s.right() : nullptr, s.frames(),
                                                 s.channels(), s.period};
            }
        }
        d.entryStart[size_t(u)] = int32_t(e);
        std::vector<std::string> keys = snd.folder.sampleKeys();
        if (!snd.sample.empty() && !contains(keys, snd.sample)) keys.push_back(snd.sample);
        for (const auto& k : stepSounds[size_t(t)]) if (!contains(keys, k)) keys.push_back(k);
        for (const auto& key : keys) {
            if (e >= entries) break;
            auto it = samples.find(key);
            if (it == samples.end() || !it->second) continue;
            const SampleData& s = *it->second;
            keep.push_back(it->second);
            d.eHash[size_t(e)] = SoundKey::hash(key);
            d.eLeft[size_t(e)] = s.left();
            d.eRight[size_t(e)] = s.right();
            d.eFrames[size_t(e)] = s.frames();
            d.eChannels[size_t(e)] = int32_t(s.channels());
            // Темп файла, атаки и метки варпинга — с таймстретчем (не перенесены): eBpm 0, атак нет.
            std::vector<SliceMark> marks;
            if (!key.empty()) {
                auto ms = snd.slices.find(key);
                if (ms != snd.slices.end())
                    for (const auto& m : ms->second) if (!m.isFree()) marks.push_back(m);
            }
            const int oct = t < int(o.octaves.size()) ? o.octaves[size_t(t)] : o.octave;
            const std::vector<int> notes = Slices::notes(marks, o.map, clipKey.first, clipKey.second, oct);
            d.eSliceStart[size_t(e)] = int32_t(sl);
            int n = 0;
            for (int k = 0; k < int(marks.size()) && sl + n < slices; ++k) {
                d.sliceAt[size_t(sl + n)] = marks[size_t(k)].at;
                d.sliceNote[size_t(sl + n)] = int32_t(notes[size_t(k)]);
                n += 1;
            }
            d.eSliceCount[size_t(e)] = int32_t(n);
            sl += n;
            e += 1;
        }
        d.entryCount[size_t(u)] = int32_t(e) - d.entryStart[size_t(u)];
    }
    d.extraStart = e;
    for (const auto& key : o.extra) {
        if (e >= entries) break;
        auto it = samples.find(key);
        if (it == samples.end() || !it->second) continue;
        const SampleData& s = *it->second;
        keep.push_back(it->second);
        d.eHash[size_t(e)] = SoundKey::hash(key);
        d.eLeft[size_t(e)] = s.left();
        d.eRight[size_t(e)] = s.right();
        d.eFrames[size_t(e)] = s.frames();
        d.eChannels[size_t(e)] = int32_t(s.channels());
        d.eSliceStart[size_t(e)] = int32_t(sl);
        d.eSliceCount[size_t(e)] = 0;
        e += 1;
    }
    d.extraCount = e - d.extraStart;
    d.entryTotal = e;
    // Референс: буферы держит снимок, пока колбэк их читает.
    if (project.arrange.ref) {
        auto it = samples.find(project.arrange.ref->key);
        if (it != samples.end() && it->second) {
            keep.push_back(it->second);
            d.refLeft = it->second->left();
            d.refRight = it->second->right();
            d.refFrames = it->second->frames();
            d.refOffset = project.arrange.ref->offset * 48000.0;
            d.refGain = float(Params::volGain(project.arrange.ref->gain));
        }
    }
    d.refB = o.refB;
    altKeep_ = chosen;
    liveCount += 1;
}

SoundSnapshot::~SoundSnapshot() { liveCount -= 1; }

void SoundSnapshot::fillClipFx(const Project& project, int tracks) {
    const int c = Params::fxCount;
    const int slots = Params::patternSlots;
    const auto& ids = Params::fxIds;
    const int n0 = std::min(int(project.tracks.size()), tracks);
    SoundData& d = data_;
    d.clipFx.assign(size_t(tracks * slots * c), 0);
    for (int t = 0; t < tracks; ++t)
        for (int k = 0; k < slots; ++k) {
            float* row = d.clipFx.data() + size_t((t * slots + k) * c);
            std::copy(d.fx.begin(), d.fx.end(), row);
            if (t >= n0 || k >= int(project.tracks[size_t(t)].clips.size())) continue;
            const auto& clip = project.tracks[size_t(t)].clips[size_t(k)];
            if (!clip || !clip->fx) continue;
            for (int p = 0; p < c; ++p) {
                auto it = clip->fx->find(ids[size_t(p)]);
                if (it != clip->fx->end()) row[p] = float(it->second);
            }
        }
}

}  // namespace jambo
