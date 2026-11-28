// Порт SeqData и SeqSnapshot (Core/Sequencer/SeqSnapshot.swift, ветка swift JAMbo).
#include "SeqSnapshot.h"
#include <cmath>
#include <map>
#include <unordered_map>
#include "model/ArrangeAuto.h"
#include "model/Project.h"
#include "sampler/SampleData.h"
#include "sampler/VoiceParam.h"

namespace jambo {

static_assert(SeqData::maxTracks == Params::maxTracks && SeqData::slots == Params::patternSlots &&
                  SeqData::maxSteps == Params::maxPages * Params::stepsPerPage,
              "размеры снимка");

int SeqSnapshot::liveCount = 0;

double SeqData::autoValue(int t, int j, double x) const {
    const size_t k = size_t(t * ArrAutoTarget::count + j);
    const int n = autoCount[k];
    if (n == 0) return -1;
    const int first = autoFirst[k];
    const int lastI = first + n - 1;
    if (x < autoX[size_t(first)] || x > autoX[size_t(lastI)]) return -1;
    int lo = first, hi = lastI;
    while (hi - lo > 1) {
        const int mid = (lo + hi) >> 1;
        if (autoX[size_t(mid)] <= x) lo = mid; else hi = mid;
    }
    const double ax = autoX[size_t(lo)], bx = autoX[size_t(hi)];
    // Разные группы (промежуток между блоками): кривой нет, кроме самой точки.
    if (autoG[size_t(lo)] != autoG[size_t(hi)])
        return x == ax ? double(autoY[size_t(lo)]) : (x == bx ? double(autoY[size_t(hi)]) : -1);
    const double ay = double(autoY[size_t(lo)]), by = double(autoY[size_t(hi)]);
    if (bx <= ax) return by;
    double f = (x - ax) / (bx - ax);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return ay + (by - ay) * f;
}

int SeqData::block(int t, int x) const {
    const int first = arrFirst[size_t(t)];
    for (int j = first + arrCount[size_t(t)] - 1; j >= first; --j) {
        const int s = blkStart[size_t(j)];
        if (s <= x) return x < s + blkLen[size_t(j)] ? j : -1;
    }
    return -1;
}

namespace {
std::map<std::string, int> indexMap(const std::vector<std::string>& ids) {
    std::map<std::string, int> m;
    for (int k = 0; k < int(ids.size()); ++k) m.emplace(ids[size_t(k)], k);
    return m;
}
const std::map<std::string, int>& voiceIndex() { static const auto m = indexMap(VoiceParam::ids()); return m; }
const std::map<std::string, int>& fxIndex() { static const auto m = indexMap(Params::fxIds); return m; }
const std::map<std::string, int>& modIndex() { static const auto m = indexMap(Params::modIds()); return m; }

/// Значение ручки дорожки без p-lock.
double trackValue(const Track& track, const std::string& id) {
    auto it = track.params.find(id);
    if (it != track.params.end()) return it->second;
    const ParamDef* d = Params::byId(id);
    return d ? d->def : 0;
}

/// P-lock громкостей на шагах MIX: бит дорожки и усиление.
void fillMix(SeqData& d, const Clip& c) {
    for (int p = 0; p < int(c.pages.size()) && p < Params::maxPages; ++p)
        for (int s = 0; s < int(c.pages[size_t(p)].size()); ++s) {
            const Step& step = c.pages[size_t(p)][size_t(s)];
            if (step.locks.empty()) continue;
            const int i = p * Params::stepsPerPage + s;
            uint16_t mask = 0;
            for (const auto& [id, v] : step.locks) {
                const int t = Params::mixTrack(id);
                if (t < 0) continue;
                mask |= uint16_t(1u << t);
                d.mixGain[size_t(i * SeqData::maxTracks + t)] = float(Params::volGain(v));
            }
            d.mixMask[size_t(i)] = mask;
        }
}

void fill(SeqData& d, const Track& track, const Clip& c, int cell, int block) {
    const PageRange r = c.range();
    d.clipBlock[size_t(cell)] = block;
    d.clipLength[size_t(cell)] = c.length;
    d.clipFrom[size_t(cell)] = r.lower;
    d.clipTo[size_t(cell)] = r.upper;
    d.clipRate[size_t(cell)] = int8_t(std::clamp(c.speed(), -4, 1));
    const double probT = trackValue(track, "prob");
    const double ratchetT = trackValue(track, "ratchet");
    auto sw = track.params.find("swing");
    const float swingT = float(sw != track.params.end() ? sw->second : Params::swing().def);
    const int maxNotes = SeqData::maxNotes;
    const int vCount = VoiceParam::count, fCount = Params::fxCount, mCount = Params::modCount;
    for (int p = 0; p < int(c.pages.size()) && p < Params::maxPages; ++p)
        for (int s = 0; s < int(c.pages[size_t(p)].size()); ++s) {
            const Step& step = c.pages[size_t(p)][size_t(s)];
            const size_t i = size_t(block * SeqData::maxSteps + p * Params::stepsPerPage + s);
            d.swing[i] = swingT;
            double prob = probT, ratchet = ratchetT;
            if (!step.locks.empty()) {
                uint32_t mask = 0, fm = 0;
                uint16_t mm = 0;
                for (const auto& [id, v] : step.locks) {
                    if (id == "prob") {
                        prob = v;
                        d.probLock[i] |= 1;
                    }
                    if (id == "ratchet") {
                        ratchet = v;
                        d.probLock[i] |= 2;
                    }
                    auto vk = voiceIndex().find(id);
                    if (vk != voiceIndex().end()) {
                        mask |= uint32_t(1) << vk->second;
                        d.lockVal[i * size_t(vCount) + size_t(vk->second)] = float(v);
                    }
                    auto fk = fxIndex().find(id);
                    if (fk != fxIndex().end()) {
                        fm |= uint32_t(1) << fk->second;
                        d.fxVal[i * size_t(fCount) + size_t(fk->second)] = float(v);
                    }
                    auto mk = modIndex().find(id);
                    if (mk != modIndex().end()) {
                        mm |= uint16_t(1u << mk->second);
                        d.modVal[i * size_t(mCount) + size_t(mk->second)] = float(v);
                    }
                }
                d.lockMask[i] = mask;
                d.fxMask[i] = fm;
                d.modMask[i] = mm;
            }
            d.prob[i] = float(prob);
            d.ratchet[i] = int32_t(std::round(ratchet));
            // Пустой шаг — значения по умолчанию уже стоят.
            if (!step.on && step.notes.empty() && step.len == 1 && !step.lens && !step.vels && !step.sound) continue;
            d.on[i] = step.on ? 1 : 0;
            d.len[i] = step.len;
            const int n = std::min(int(step.notes.size()), maxNotes);
            d.noteCount[i] = uint8_t(n);
            for (int k = 0; k < n; ++k) {
                d.notes[i * size_t(maxNotes) + size_t(k)] = int16_t(step.notes[size_t(k)]);
                d.noteLen[i * size_t(maxNotes) + size_t(k)] = step.noteLen(k);
                d.noteVel[i * size_t(maxNotes) + size_t(k)] = float(step.noteVel(k).value_or(-1));
            }
            d.split[i] = step.lens || step.vels ? 1 : 0;
            if (step.sound) d.sound[i] = SoundKey::hash(*step.sound);
        }
}
}  // namespace

SeqSnapshot::SeqSnapshot(const Project& project) {
    const int tracks = SeqData::maxTracks;
    const int used = std::min(int(project.tracks.size()), tracks);
    SeqData& d = data_;
    d.cellFirst.resize(size_t(tracks + 1));
    auto ownCells = std::vector<std::unordered_map<const Clip*, int>>(size_t(tracks));
    int cells = 0;
    int blocks = 0;
    for (int t = 0; t < tracks; ++t) {
        d.cellFirst[size_t(t)] = cells;
        cells += SeqData::slots;
        if (t >= used) continue;
        const Track& tr = project.tracks[size_t(t)];
        for (const auto& c : tr.clips) if (c) ++blocks;
        auto& own = ownCells[size_t(t)];
        for (const auto& b : tr.blocks) {
            if (b.pattern && own.try_emplace(&*b.pattern, SeqData::slots + int(own.size())).second) ++cells;
        }
        blocks += int(own.size());
    }
    d.cellFirst[size_t(tracks)] = cells;
    const size_t steps = size_t(std::max(1, blocks) * SeqData::maxSteps);
    size_t arrBlocks = 1;
    for (int t = 0; t < used; ++t) arrBlocks += project.tracks[size_t(t)].blocks.size();
    size_t autoPoints = 1;
    for (int t = 0; t < used; ++t)
        for (const auto& b : project.tracks[size_t(t)].blocks)
            for (const auto& l : b.autos) autoPoints += l.points.size();
    const size_t autoCells = size_t(tracks * ArrAutoTarget::count);
    const size_t mn = SeqData::maxNotes;
    d.stopped.assign(size_t(tracks), 1);
    d.slot.assign(size_t(tracks), 0);
    d.clipBlock.assign(cells, -1);
    d.clipLength.assign(cells, Params::stepsPerPage);
    d.clipFrom.assign(cells, 0);
    d.clipTo.assign(cells, 0);
    d.clipRate.assign(cells, 0);
    d.on.assign(steps, 0);
    d.len.assign(steps, 1);
    d.noteCount.assign(steps, 0);
    d.notes.assign(steps * mn, 0);
    d.split.assign(steps, 0);
    d.noteLen.assign(steps * mn, 1);
    d.noteVel.assign(steps * mn, -1);
    d.prob.assign(steps, 100);
    d.ratchet.assign(steps, 0);
    d.probLock.assign(steps, 0);
    d.swing.assign(steps, 50);
    d.sound.assign(steps, 0);
    d.lockMask.assign(steps, 0);
    d.lockVal.assign(steps * VoiceParam::count, 0);
    d.fxMask.assign(steps, 0);
    d.fxVal.assign(steps * Params::fxCount, 0);
    d.modMask.assign(steps, 0);
    d.modVal.assign(steps * Params::modCount, 0);
    d.repeats.assign(size_t(SeqData::slots), 1);
    d.rows = project.visibleRows();
    d.arrStart = std::min(project.arrStart, d.rows - 1);
    d.arrEndStop = project.arrEndStop;
    d.rowLoop = project.rowLoop;
    for (int k = 0; k < SeqData::slots; ++k) d.repeats[size_t(k)] = project.scenes[size_t(k)].repeats;
    d.mixMask.assign(size_t(SeqData::maxSteps), 0);
    d.mixGain.assign(size_t(SeqData::maxSteps * tracks), 0);
    d.chanLength.assign(SeqData::chans, 0);
    d.chanFrom.assign(SeqData::chans, 0);
    d.chanTo.assign(SeqData::chans, 0);
    for (int c = 0; c < SeqData::chans; ++c) {
        const Clip& clip = project.chanSeqs[size_t(c)].openClip();
        d.chanLength[size_t(c)] = clip.length;
        d.chanFrom[size_t(c)] = clip.range().lower;
        d.chanTo[size_t(c)] = clip.range().upper;
    }
    fillMix(d, project.chanSeqs[Project::mixSeq].openClip());
    d.arrFirst.assign(size_t(tracks), 0);
    d.arrCount.assign(size_t(tracks), 0);
    d.blkClip.assign(arrBlocks, 0);
    d.blkStart.assign(arrBlocks, 0);
    d.blkLen.assign(arrBlocks, 0);
    d.blkOff.assign(arrBlocks, 0);
    int nb = 0;
    for (int t = 0; t < used; ++t) {
        const Track& tr = project.tracks[size_t(t)];
        d.arrFirst[size_t(t)] = nb;
        const auto& own = ownCells[size_t(t)];
        for (const auto& b : tr.blocks) {
            // Повторы с той же копией используют один набор шагов; правка блока даёт свою ячейку.
            d.blkClip[size_t(nb)] = b.pattern ? own.at(&*b.pattern) : b.clip;
            d.blkStart[size_t(nb)] = b.start;
            d.blkLen[size_t(nb)] = b.length;
            d.blkOff[size_t(nb)] = b.offset;
            ++nb;
        }
        d.arrCount[size_t(t)] = int32_t(tr.blocks.size());
    }
    d.autoFirst.assign(autoCells, 0);
    d.autoCount.assign(autoCells, 0);
    d.autoX.assign(autoPoints, 0);
    d.autoY.assign(autoPoints, 0);
    d.autoG.assign(autoPoints, 0);
    d.autoAny.assign(size_t(tracks), 0);
    size_t na = 0;
    for (int t = 0; t < used; ++t) {
        const Track& tr = project.tracks[size_t(t)];
        for (const auto& l : tr.autos) {
            const int j = ArrAutoTarget::index(l.param);
            if (j < 0) continue;
            const auto groups = tr.autoGroups(l.param);
            size_t n = 0;
            for (const auto& g : groups) n += g.size();
            if (n == 0 || na + n > autoPoints) continue;
            const size_t k = size_t(t * ArrAutoTarget::count + j);
            d.autoFirst[k] = int32_t(na);
            d.autoCount[k] = int32_t(n);
            for (int g = 0; g < int(groups.size()); ++g)
                for (const auto& p : groups[size_t(g)]) {
                    d.autoX[na] = p.x;
                    d.autoY[na] = float(p.y);
                    d.autoG[na] = g;
                    ++na;
                }
            d.autoAny[size_t(t)] = 1;
        }
    }
    int end = 0;
    for (const auto& tr : project.tracks)
        for (const auto& b : tr.blocks) end = std::max(end, b.end());
    d.arrEnd = end;
    d.loopFrom = project.arrange.loopFrom;
    d.loopTo = project.arrange.loopTo;
    d.loop = project.arrange.loop && project.arrange.hasLoop();
    d.trackCount = used;
    int block = 0;
    for (int t = 0; t < used; ++t) {
        const Track& track = project.tracks[size_t(t)];
        d.stopped[size_t(t)] = track.stopped || track.blank() ? 1 : 0;
        d.slot[size_t(t)] = track.slot;
        for (int k = 0; k < SeqData::slots; ++k) {
            if (!track.clips[size_t(k)]) continue;
            fill(d, track, *track.clips[size_t(k)], d.cellFirst[size_t(t)] + k, block);
            ++block;
        }
        for (const auto& [pattern, cell] : ownCells[size_t(t)]) {
            fill(d, track, *pattern, d.cellFirst[size_t(t)] + cell, block);
            ++block;
        }
    }
    liveCount += 1;
}

bool SeqSnapshot::same(const Project& a, const Project& b) {
    if (a.tracks.size() != b.tracks.size() || a.chanSeqs != b.chanSeqs || a.scenes != b.scenes || a.rows != b.rows ||
        a.arrStart != b.arrStart || a.arrEndStop != b.arrEndStop || a.rowLoop != b.rowLoop)
        return false;
    if (a.arrange.loop != b.arrange.loop || a.arrange.loopFrom != b.arrange.loopFrom ||
        a.arrange.loopTo != b.arrange.loopTo)
        return false;
    for (size_t k = 0; k < a.tracks.size(); ++k) {
        const Track& x = a.tracks[k];
        const Track& y = b.tracks[k];
        if (x.slot != y.slot || x.stopped != y.stopped || x.clips != y.clips || x.blocks != y.blocks || x.autos != y.autos)
            return false;
        for (const char* id : {"prob", "ratchet", "swing"}) {
            auto i = x.params.find(id), j = y.params.find(id);
            const bool hx = i != x.params.end(), hy = j != y.params.end();
            if (hx != hy || (hx && i->second != j->second)) return false;
        }
    }
    return true;
}

}  // namespace jambo
