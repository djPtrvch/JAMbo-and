// Весь звук приложения (порт Core/Engine/AudioCore.swift).
#include "AudioCore.h"
#include <algorithm>
#include "Capture.h"
#include "model/Project.h"
#include "sequencer/TempoSource.h"

namespace jambo {

AudioCore::AudioCore(bool m) : useMaster(m), masterL(maxFrames, 0.f), masterR(maxFrames, 0.f) {
    sequencer.sampler = &sampler;
    sampler.fx = &fx;
    fx.pool = &pool;
    sampler.pool = &pool;
    sampler.mixLockMask = &sequencer.mixLockMask;
    sampler.mixLockGain = sequencer.mixLockGain.data();
    sampler.mod = &mod;
    sampler.eqs = &trackEqs;
    sampler.spectra = &spectra;
    sampler.levels = &levels;
    sampler.profile = &profile;
    master.spectra = &spectra;
    master.profile = &profile;
    // Источники T1…T16 динамических полос EQ мастера — выходы дорожек.
    master.eq->trackL = sampler.outL.data();
    master.eq->trackR = sampler.outR.data();
    master.eq->trackStride = Sampler::maxFrames;
    sampler.eqDyn = &eqDyn;
    master.eqDyn = &eqDyn;
    sampler.eqListen = &eqListen;
    master.eqListen = &eqListen;
    sequencer.mod = &mod;
}

AudioCore::~AudioCore() {
    pool.stop();
    while (auto s = snapshots.pop()) delete *s;
    while (auto s = retired.pop()) delete *s;
    while (auto s = sounds.pop()) delete *s;
    while (auto s = retiredSounds.pop()) delete *s;
    for (auto& w : waiting) {
        delete w.seq;
        delete w.sound;
    }
    sampler.synth = nullptr;
    delete current;
    delete currentSound;
}

void AudioCore::publish(const Project& project, const std::map<std::string, SampleRef>& samples,
                        SoundSnapshot::Options o, double now) {
    releaseRetired(now);
    if (!lastSeq || !SeqSnapshot::same(*lastSeq, project)) {
        lastSeq = std::make_unique<Project>(project);
        auto* snap = new SeqSnapshot(project);
        if (!snapshots.push(snap)) delete snap;
    }
    o.slots = sequencer.launcher.slot.data();
    o.keepAlt = altKeep;
    auto* made = new SoundSnapshot(project, samples, o);
    altKeep = made->altKeep();
    if (!sounds.push(made)) delete made;
}

void AudioCore::installSynth() {
    if (synthInstalled) return;
    synthInstalled = true;
    synthOwned = std::make_unique<SynthVoices>(sampleRate);
    synthQueue.push(synthOwned.get());
}

void AudioCore::releaseRetired(double now) {
    while (auto item = retired.pop()) waiting.push_back({now, *item, nullptr});
    while (auto item = retiredSounds.pop()) waiting.push_back({now, nullptr, *item});
    for (size_t k = 0; k < waiting.size();) {
        if (now - waiting[k].time >= keepAlive) {
            delete waiting[k].seq;
            delete waiting[k].sound;
            waiting.erase(waiting.begin() + long(k));
        } else {
            ++k;
        }
    }
}

void AudioCore::render(int frames, float* left, float* right, double host) {
    profile.begin();
    // Сначала снимок проекта, потом команды: PLAY сразу после правки видит новые клипы и строки.
    takeSnapshots();
    // Автоматизация на таймлайне: кривые — вместо ручек, до модуляции, FX и голосов.
    if (SoundData* d = sampler.sound()) {
        sequencer.applyAuto(*d, sampleTime_);
        mod.begin(*d, frames, sequencer.modLockMask.data(), sequencer.modLockVal.data(), sequencer.bpm());
        fx.begin(*d, sequencer.fxLockMask.data(), sequencer.fxLockVal.data(), &mod, sampleTime_);
    }
    sampler.bpm = sequencer.bpm();
    while (auto e = events.pop()) apply(*e);
    std::fill(left, left + frames, 0.f);
    std::fill(right, right + frames, 0.f);
    tone.render(frames, left, right);
    sequencer.process(sampleTime_, frames, left, right, host);
    renderArp(frames);
    profile.lap(RenderProfile::seq);
    renderMix(frames, left, right);
    if (useMaster) renderPhones(frames, left, right);
    mod.publish(sampleTime_ + int64_t(frames));
    sampleTime_ += int64_t(frames);
    frameCounter.store(long(sampleTime_));
    profile.lap(RenderProfile::meters);
    profile.end(frames);
}

void AudioCore::renderArp(int frames) {
    const double sf = 60 / sequencer.bpm() / TempoGrid::stepsPerBeat * sampleRate;
    arp.process(sampleTime_, frames, sequencer.playing(), sequencer.tick(), sequencer.stepFrame(sequencer.tick()), sf);
    for (int i = 0; i < arp.emitCount; ++i) {
        const size_t I = size_t(i);
        const int t = int(arp.emitTrack[I]);
        const int64_t f = arp.emitFrame[I];
        const int id = Arpeggiator::noteId(t);
        if (arp.emitKind[I] == 1) {
            mod.liveOn(t, f);
            sampler.noteOn(t, int(arp.emitNote[I]), id, arp.emitVel[I], f);
        } else {
            mod.liveOff(t, f);
            sampler.release(t, id, f);
        }
    }
    arp.clearEmit();
}

void AudioCore::renderMix(int frames, float* left, float* right) {
    SoundData* d = sampler.sound();
    if (!d) return;
    const int n = std::min(frames, maxFrames);
    StemRing& ring = sequencer.stemRing;
    sampler.stemGate = ring.capturing() ? ring.gate.data() : nullptr;
    levels.begin();
    if (useMaster) {
        float* ml = masterL.data();
        float* mr = masterR.data();
        std::fill(ml, ml + n, 0.f);
        std::fill(mr, mr + n, 0.f);
        sampler.render(sampleTime_, n, ml, mr);
        renderRef(*d, n);
        master.process(ml, mr, n, d->master.data(), d->eq.data() + EqSpec::masterChannel * EqSpec::stride);
        profile.lap(RenderProfile::master);
        if (sampler.stemGate) captureStems(ring, n, ml, mr);
        meterMaster(n);
        master.scope.gonio(ml, mr, n);
        // UNITY — только слышимый звук.
        master.applyUnity(ml, mr, n);
        ref.mixPost(ml, mr, n);
        for (int i = 0; i < n; ++i) {
            left[i] += ml[i];
            right[i] += mr[i];
        }
    } else {
        sampler.render(sampleTime_, n, left, right);
        if (sampler.stemGate) captureStems(ring, n, left, right);
    }
    profile.lap(RenderProfile::meters);
    sampler.renderPreview(n, left, right);
    profile.lap(RenderProfile::voices);
    levels.commit(n);
}

void AudioCore::renderPhones(int frames, float* left, float* right) {
    const int n = std::min(frames, maxFrames);
    const bool shown = spectra.channel.load() == EqSpec::phonesChannel;
    if (shown) spectra.pre.push(left, right, n);
    const SoundData* d = sampler.sound();
    const bool raw = (d && d->refB) && ref.throughMonitor.load() == 0;
    monitor.process(left, right, n, raw);
    if (shown) spectra.post.push(left, right, n);
}

void AudioCore::renderRef(const SoundData& d, int n) {
    const bool play = sequencer.playing() && sequencer.launcher.arrangeMode;
    double x = 0;
    if (play) {
        const double end = sequencer.arrangePosition(sampleTime_ + int64_t(n));
        const double back = double(n) / sampleRate * sequencer.bpm() / 15;
        x = end - back;
    }
    ref.render(d, play, x, sequencer.bpm(), n);
    ref.mixPre(d.refB, masterL.data(), masterR.data(), n, levels);
}

void AudioCore::captureStems(StemRing& ring, int n, const float* ml, const float* mr) {
    const int mf = Sampler::maxFrames;
    for (int t = 0; t < SeqData::maxTracks; ++t)
        ring.captureBlock(t, sampler.outL.data() + t * mf, sampler.outR.data() + t * mf, ring.gate.data() + t * mf,
                          sampleTime_, n);
    ring.captureBlock(StemRing::master, ml, mr, nullptr, sampleTime_, n);
    // REC редактора: тот же звук, что у стема источника.
    const int src = ring.captureSource;
    if (src == StemRing::master) {
        ring.captureBlock(StemRing::capture, ml, mr, nullptr, sampleTime_, n);
    } else if (src >= 0 && src < SeqData::maxTracks) {
        ring.captureBlock(StemRing::capture, sampler.outL.data() + src * mf, sampler.outR.data() + src * mf,
                          ring.gate.data() + src * mf, sampleTime_, n);
    }
}

void AudioCore::meterMaster(int n) {
    const float* l = masterL.data();
    const float* r = masterR.data();
    float pl = 0, pr = 0;
    for (int i = 0; i < n; ++i) {
        const float al = l[i] < 0 ? -l[i] : l[i];
        const float ar = r[i] < 0 ? -r[i] : r[i];
        if (al > pl) pl = al;
        if (ar > pr) pr = ar;
    }
    float* m = levels.current(Levels::master);
    m[0] = pl;
    m[1] = pr;
    if (levels.loudness.listens(Levels::master)) levels.loudness.process(l, r, n);
    if (levels.mixLoudness.listens(Levels::master)) levels.mixLoudness.process(l, r, n);
}

void AudioCore::takeSnapshots() {
    while (auto item = synthQueue.pop()) {
        SynthVoices* v = *item;
        if (!v) continue;
        v->mod = &mod;
        v->pool = &pool;
        sampler.synth = v;
    }
    while (auto item = snapshots.pop()) {
        SeqSnapshot* snap = *item;
        if (!snap) continue;
        if (current) retired.push(current);
        current = snap;
        sequencer.setData(&snap->data());
    }
    while (auto item = sounds.pop()) {
        SoundSnapshot* snap = *item;
        if (!snap) continue;
        if (currentSound) retiredSounds.push(currentSound);
        currentSound = snap;
        sampler.adopt(snap->mutableData(), sampleTime_);
    }
}

void AudioCore::apply(const CoreEvent& e) {
    using K = CoreEvent::Kind;
    switch (e.kind) {
    case K::none:
        break;
    case K::tone:
        tone.setOn(e.on);
        break;
    case K::toneFrequency:
        tone.setFrequency(double(e.f));
        break;
    case K::play:
        sequencer.setPlaying(e.on, sampleTime_);
        // Стоп: будущие триги Sidechain отменяются.
        if (!e.on) fx.flush();
        break;
    case K::playAt:
        sequencer.start(e.frame, sampleTime_);
        break;
    case K::clockPhase:
        sequencer.clockPhase(e.a, e.x);
        break;
    case K::tempo:
        sequencer.setBpm(e.x);
        break;
    case K::metronome:
        sequencer.metronome = e.on;
        break;
    case K::metronomeGain:
        sequencer.setMetronomeGain(double(e.f));
        break;
    case K::launch:
        sequencer.queue(e.a, e.b);
        break;
    case K::scene:
        sequencer.queueScene(e.a, e.on);
        break;
    case K::adoptPlay:
        sequencer.adoptPlay();
        break;
    case K::arrange:
        sequencer.setArrange(e.on, e.a);
        break;
    case K::returnToArrange:
        sequencer.returnToArrange(e.a);
        break;
    case K::leaveArrange:
        sequencer.leaveArrange();
        break;
    case K::seekArrange:
        sequencer.seekArrange(e.a);
        break;
    case K::autoHold:
        sequencer.autoPlay.setHold(e.a, e.b, e.on);
        break;
    case K::moveTrack:
        sequencer.launcher.move(e.a, e.b);
        break;
    case K::noteOn:
        // Дорожка с ARP: нота — в набор арпеджио, сама не звучит.
        if (arp.active(e.a)) {
            arp.press(e.a, e.b, e.c, e.f, sampleTime_);
            break;
        }
        mod.liveOn(e.a, sampleTime_);
        sampler.noteOn(e.a, e.b, e.c, e.f, sampleTime_);
        break;
    case K::noteOff:
        if (arp.active(e.a)) {
            arp.release(e.a, e.c, sampleTime_);
            // Нота, нажатая до включения ARP, звучит сама — гаснет как обычно.
            sampler.release(e.a, e.c, sampleTime_);
            break;
        }
        mod.liveOff(e.a, sampleTime_);
        sampler.release(e.a, e.c, sampleTime_);
        break;
    case K::arp:
        arp.configure(e.a, e.on, e.b, e.c, e.d, double(e.f), e.latch, sampleTime_);
        break;
    case K::preview:
        sampler.preview(e.hash, e.x, e.y, sampleTime_);
        break;
    case K::previewStop:
        sampler.stopPreview(sampleTime_);
        break;
    case K::warpPreview:
        sampler.warpPreview(e.a, e.hash, sampleTime_);
        break;
    case K::presetNote:
        sampler.presetNote(e.a, e.b, e.hash, e.locks, sampleTime_);
        break;
    case K::stemArm:
        sequencer.armStem(e.a, e.on);
        break;
    case K::stemFinish:
        sequencer.finishStem(e.a);
        break;
    case K::stopAfterStems:
        if (!sequencer.requestStemStop()) {
            const bool was = sequencer.playing();
            sequencer.setPlaying(false, sampleTime_);
            fx.flush();
            // Интерфейс ждал конца записи — транспорт остановился сразу.
            if (was) sequencer.out.push(SeqOut::ended());
        }
        break;
    case K::captureStart: {
        StemRing& ring = sequencer.stemRing;
        if (ring.isReady() && !ring.isActive(StemRing::capture)) {
            ring.captureSource = e.a;
            ring.begin(StemRing::capture, sampleTime_, Capture::maxFrames);
        }
        break;
    }
    case K::captureStop:
        sequencer.stemRing.end(StemRing::capture, sampleTime_);
        break;
    }
}

}  // namespace jambo
