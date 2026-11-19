// Порт Core/Sequencer/Sequencer.swift (ветка swift JAMbo).
#include "Sequencer.h"
#include <cmath>
#include "model/Project.h"

namespace jambo {

Sequencer::Sequencer(double sr, double bpm) : launcher(out), sampleRate(sr), tempo(bpm), wantedBpm(bpm), click(sr) {
    const size_t pm = pendingMax;
    pendingTime.assign(pm, 0);
    pendingTrack.assign(pm, 0);
    pendingNotes.assign(pm * SeqData::maxNotes, 0);
    pendingCountN.assign(pm, 0);
    pendingMask.assign(pm, 0);
    pendingVals.assign(pm * VoiceParam::count, 0);
    pendingSound.assign(pm, 0);
    pendingGate.assign(pm, -1);
    pendingEnv.assign(pm, 0);
    modLockMask.assign(SeqData::maxTracks, 0);
    modLockVal.assign(size_t(SeqData::maxTracks * Params::modCount), 0);
    fxLockMask.assign(SeqData::maxTracks, 0);
    fxLockVal.assign(size_t(SeqData::maxTracks * Params::fxCount), 0);
    mixLockGain.assign(SeqData::maxTracks, 0);
}

void Sequencer::setData(const SeqData* d) {
    data_ = d;
    if (!playing_ && d) launcher.adopt(*d);
}

void Sequencer::setBpm(double bpm) {
    wantedBpm = bpm;
    if (!playing_) tempo.set(bpm);
}

void Sequencer::setPlaying(bool on, int64_t at) {
    if (on == playing_) return;
    playing_ = on;
    pendingCount = 0;
    if (on) {
        tick_ = 0;
        tempo.set(wantedBpm);
        tempo.start(double(at) / sampleRate);
        if (data_) launcher.start(*data_);
    } else {
        // Стемы дописываются в кадре остановки.
        stemsStopped(at);
        if (sampler) sampler->midiAllOff(at);
        launcher.reset();
        std::fill(fxLockMask.begin(), fxLockMask.end(), 0);
        std::fill(modLockMask.begin(), modLockMask.end(), 0);
        mixLockMask = 0;
        for (int t = 0; t < SeqData::maxTracks + SeqData::chans; ++t) out.push(SeqOut::step(t, -1));
        out.push(SeqOut::scene(-1, 0, 16));
        out.push(SeqOut::sceneQueue(-1));
    }
}

void Sequencer::start(int64_t frame, int64_t now) {
    if (playing_) return;
    const double step = 60 / wantedBpm / TempoGrid::stepsPerBeat * sampleRate;
    const int64_t earliest = now - int64_t(step * 0.9);
    setPlaying(true, std::max(frame, earliest));
}

void Sequencer::clockPhase(int step, double frame) {
    if (!(playing_ && step == tick_)) return;
    const double err = stepFrame(step) - frame;
    const double len = 60 / tempo.tempo() / TempoGrid::stepsPerBeat * sampleRate;
    if (std::fabs(err) >= len) return;
    tempo.shift(-err * 0.25 / sampleRate);
}

void Sequencer::adoptPlay() {
    if (!data_) return;
    launcher.adopt(*data_);
    launcher.clearQueues();
}

void Sequencer::seekArrange(int pos) {
    const int p = std::max(0, pos);
    launcher.arrFrom = p;
    if (!playing_) return;
    launcher.seek(p);
}

void Sequencer::returnToArrange(int t) {
    if (!playing_) return;
    if (t < 0) {
        for (int k = 0; k < SeqData::maxTracks; ++k) launcher.returnToArrange(k);
    } else {
        launcher.returnToArrange(t);
    }
}

double Sequencer::arrangePosition(int64_t frame) const {
    if (!playing_) return -1;
    const double pos = double(launcher.arrPos);
    if (tick_ <= 0) return pos;
    const double a = stepFrame(tick_ - 1);
    const double b = stepFrame(tick_);
    if (b <= a) return pos;
    double f = (double(frame) - a) / (b - a);
    if (f < 0) f = 0;
    if (f > 0.999) f = 0.999;
    return pos + f;
}

void Sequencer::applyAuto(SoundData& snd, int64_t frame) {
    if (!data_) return;
    autoPlay.apply(*data_, snd, arrangePosition(frame), launcher.follow.get());
}

void Sequencer::process(int64_t blockStart, int frames, float* left, float* right, double host) {
    const double end = double(blockStart + frames);
    if (playing_ && data_) {
        const SeqData& d = *data_;
        int guardSteps = 0;
        while (playing_ && stepFrame(tick_) < end && guardSteps < 64) {
            playStep(d, stepFrame(tick_));
            tempo.retempo(wantedBpm, double(tick_ - 1) / TempoGrid::stepsPerBeat);
            guardSteps += 1;
        }
    }
    firePending(end);
    click.render(blockStart, frames, left, right);
    PlayClock::State st;
    st.playing = playing_;
    st.nextTick = tick_;
    st.nextFrame = stepFrame(tick_);
    st.stepFrames = 60 / tempo.tempo() / TempoGrid::stepsPerBeat * sampleRate;
    st.blockFrame = double(blockStart);
    st.blockHost = host;
    clock.write(st);
}

void Sequencer::playStep(const SeqData& d, double time) {
    if (!launcher.beforeStep(d, tick_)) {
        setPlaying(false, int64_t(time));
        out.push(SeqOut::ended());
        return;
    }
    // Автоматизация — на позицию этого шага, до его тригов.
    if (sampler) {
        if (SoundData* snd = sampler->sound())
            autoPlay.apply(d, *snd, double(launcher.arrPos), launcher.follow.get());
    }
    // Стемы — после смены строк и клипов на этом тике; PLAY во время записи — стоп, когда всё дописано.
    if (stemsStep(d, time)) {
        setPlaying(false, int64_t(time));
        out.push(SeqOut::ended());
        return;
    }
    const double dur = 60 / tempo.tempo() / TempoGrid::stepsPerBeat * sampleRate;
    if (tick_ % int(TempoGrid::stepsPerBeat) == 0) {
        const bool accent = TempoGrid::isBarStart(double(tick_) / TempoGrid::stepsPerBeat);
        if (metronome) click.trigger(int64_t(time), accent);
        out.push(SeqOut::beat(accent));
    }
    // Свои последовательности MIX и MST: только p-lock на время шага.
    for (int c = 0; c < SeqData::chans; ++c) {
        const int pos = d.chanIndex(c, tick_);
        out.push(SeqOut::step(SeqData::maxTracks + c, pos));
        if (c == Project::mixSeq) {
            const uint16_t m = pos >= 0 && pos < SeqData::maxSteps ? d.mixMask[size_t(pos)] : 0;
            mixLockMask = m;
            if (m != 0)
                for (int t = 0; t < SeqData::maxTracks; ++t)
                    mixLockGain[size_t(t)] = d.mixGain[size_t(pos * SeqData::maxTracks + t)];
        }
    }
    for (int t = 0; t < d.trackCount; ++t) {
        if (!launcher.plays(d, t)) {
            out.push(SeqOut::step(t, -1));
            fxLockMask[size_t(t)] = 0;
            modLockMask[size_t(t)] = 0;
            continue;
        }
        // Скорость клипа: ×2 — два шага за тик, медленнее — шаг раз в 2…16 тиков.
        const int k = launcher.slot[size_t(t)];
        const int len = d.playLen(t, k);
        const int rate = d.rate(t, k);
        const int pass = d.tickLen(t, k);
        const int o = ((tick_ - launcher.trackStart[size_t(t)]) % pass + pass) % pass;
        int n = o, count = 1;
        if (rate > 0) {
            n = o * 2;
            count = 2;
        } else if (rate < 0) {
            const int div = 1 << (-rate);
            if (o % div != 0) continue;
            n = o / div;
        }
        const double stepDur = rate > 0 ? dur / 2 : dur * double(1 << std::max(0, -rate));
        for (int h = 0; h < count && n + h < len; ++h) {
            const int pos = d.playIndex(t, k, n + h);
            const int i = d.stepIndex(t, k, pos);
            if (h == 0) {
                out.push(SeqOut::step(t, pos));
                fxLockMask[size_t(t)] = i >= 0 ? d.fxMask[size_t(i)] : 0;
                if (i >= 0 && d.fxMask[size_t(i)] != 0)
                    for (int p = 0; p < Params::fxCount; ++p)
                        fxLockVal[size_t(t * Params::fxCount + p)] = d.fxVal[size_t(i * Params::fxCount + p)];
                modLockMask[size_t(t)] = i >= 0 ? d.modMask[size_t(i)] : 0;
                if (i >= 0 && d.modMask[size_t(i)] != 0)
                    for (int p = 0; p < Params::modCount; ++p)
                        modLockVal[size_t(t * Params::modCount + p)] = d.modVal[size_t(i * Params::modCount + p)];
            }
            if (i >= 0 && d.on[size_t(i)] != 0) trig(d, t, i, pos, time + double(h) * stepDur, stepDur);
        }
    }
    tick_ += 1;
}

void Sequencer::trig(const SeqData& d, int t, int i, int pos, double time, double dur) {
    double prob = double(d.prob[size_t(i)]);
    double ratchetV = double(d.ratchet[size_t(i)]);
    const double swing = double(d.swing[size_t(i)]);
    // Автоматизация Chance и Ratchet: дорожка на таймлайне, на шаге нет их p-lock.
    if (launcher.follow[size_t(t)] && d.autoAny[size_t(t)] != 0) {
        const double x = double(launcher.arrPos);
        const SoundData* snd = sampler ? sampler->sound() : nullptr;
        if ((d.probLock[size_t(i)] & 1) == 0) prob = autoPlay.step(d, snd, t, ArrAutoTarget::prob, x, prob);
        if ((d.probLock[size_t(i)] & 2) == 0) ratchetV = autoPlay.step(d, snd, t, ArrAutoTarget::ratchet, x, ratchetV);
    }
    if (mod && mod->modulated(t)) {
        const int64_t f = int64_t(time);
        prob = mod->value(t, ModTarget::prob, prob, f);
        ratchetV = mod->value(t, ModTarget::ratchet, ratchetV, f);
    }
    if (double(nextRandom()) * 100 >= prob) return;
    out.push(SeqOut::fired(t, pos));
    const int ratchet = int(std::round(ratchetV)) + 1;
    const double offset = pos % 2 == 1 ? (swing - 50) / 50 * dur : 0;
    const double sub = dur / double(ratchet);
    const int len = d.len[size_t(i)];
    // Нота от 2 шагов держится до своего конца; в 1 шаг и рэтчет — Decay сразу после атаки.
    const int64_t gate = ratchet == 1 && len >= 2 ? int64_t(std::llround(double(len) * dur)) : -1;
    const int64_t envGate =
        int64_t(std::llround(ratchet > 1 ? sub * 0.6 : (len >= 2 ? double(len) * dur : dur * 0.9)));
    for (int r = 0; r < ratchet; ++r) {
        const double at = time + offset + double(r) * sub;
        if (d.split[size_t(i)] == 0) {
            schedule(t, at, d, i, gate, envGate);
            continue;
        }
        // Ноты со своими длинами и velocity — по одной; с конца, чтобы у DRUM осталась первая.
        for (int j = int(d.noteCount[size_t(i)]) - 1; j >= 0; --j) {
            const int32_t nl = d.noteLen[size_t(i * SeqData::maxNotes + j)];
            const int64_t ng = ratchet == 1 && nl >= 2 ? int64_t(std::llround(double(nl) * dur)) : -1;
            scheduleNote(t, at, d, i, j, ng, envGate);
        }
    }
}

void Sequencer::schedule(int track, double time, const SeqData& d, int i, int64_t gate, int64_t envGate) {
    if (pendingCount >= pendingMax) return;
    const int k = pendingCount;
    const size_t mn = SeqData::maxNotes, vc = VoiceParam::count;
    pendingTime[size_t(k)] = time;
    pendingTrack[size_t(k)] = track;
    const int n = int(d.noteCount[size_t(i)]);
    pendingCountN[size_t(k)] = n;
    for (int j = 0; j < n; ++j) pendingNotes[size_t(k) * mn + size_t(j)] = d.notes[size_t(i) * mn + size_t(j)];
    pendingMask[size_t(k)] = d.lockMask[size_t(i)];
    for (size_t p = 0; p < vc; ++p) pendingVals[size_t(k) * vc + p] = d.lockVal[size_t(i) * vc + p];
    pendingSound[size_t(k)] = d.sound[size_t(i)];
    pendingGate[size_t(k)] = gate;
    pendingEnv[size_t(k)] = envGate;
    pendingCount += 1;
}

void Sequencer::scheduleNote(int track, double time, const SeqData& d, int i, int j, int64_t gate, int64_t envGate) {
    if (pendingCount >= pendingMax) return;
    const int k = pendingCount;
    schedule(track, time, d, i, gate, envGate);
    if (pendingCount == k) return;
    pendingCountN[size_t(k)] = 1;
    pendingNotes[size_t(k * SeqData::maxNotes)] = d.notes[size_t(i * SeqData::maxNotes + j)];
    const float v = d.noteVel[size_t(i * SeqData::maxNotes + j)];
    if (v >= 0) {
        pendingMask[size_t(k)] |= VoiceMask(1) << VoiceParam::level;
        pendingVals[size_t(k * VoiceParam::count + VoiceParam::level)] = v;
    }
}

void Sequencer::movePending(int from, int to) {
    const size_t f = size_t(from), t = size_t(to), mn = SeqData::maxNotes, vc = VoiceParam::count;
    pendingTime[t] = pendingTime[f];
    pendingTrack[t] = pendingTrack[f];
    pendingCountN[t] = pendingCountN[f];
    for (size_t j = 0; j < mn; ++j) pendingNotes[t * mn + j] = pendingNotes[f * mn + j];
    pendingMask[t] = pendingMask[f];
    for (size_t p = 0; p < vc; ++p) pendingVals[t * vc + p] = pendingVals[f * vc + p];
    pendingSound[t] = pendingSound[f];
    pendingGate[t] = pendingGate[f];
    pendingEnv[t] = pendingEnv[f];
}

void Sequencer::firePending(double end) {
    int k = 0;
    while (k < pendingCount) {
        const size_t s = size_t(k);
        if (pendingTime[s] >= end) {
            k += 1;
            continue;
        }
        const int t = pendingTrack[s];
        const int64_t frame = int64_t(std::floor(pendingTime[s]));
        out.push(SeqOut::trig(t, frame));
        // ENV запускается в кадре ноты.
        if (mod) mod->trig(t, frame, pendingEnv[s]);
        const int16_t* notes = &pendingNotes[s * SeqData::maxNotes];
        const float* vals = &pendingVals[s * VoiceParam::count];
        if (sampler && sampler->sendsMidi(t)) {
            sampler->sidechainTrig(t, frame);
            sampler->midiNotes(t, frame, notes, pendingCountN[s], pendingMask[s], vals, pendingEnv[s], -1);
        } else if (sampler) {
            sampler->trigger(t, frame, notes, pendingCountN[s], pendingMask[s], vals, pendingSound[s], pendingGate[s],
                             pendingSound[s] != 0, -1);
        }
        pendingCount -= 1;
        if (k != pendingCount) movePending(pendingCount, k);
    }
}

float Sequencer::nextRandom() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng >> 8) / float(1 << 24);
}

}  // namespace jambo
