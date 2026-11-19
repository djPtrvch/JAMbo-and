// Порт Core/Synth/SynthVoices.swift (ветка swift JAMbo): запуск и звук голосов.
#include "SynthVoices.h"
#include <cmath>
#include "Jambo.h"
#include "model/PitchKey.h"
#include "model/SynthEngines.h"

namespace jambo {

namespace {
constexpr float fpi = 3.14159265358979323846f;
}

SynthVoices::SynthVoices(double sampleRate)
    : bank(slots, sampleRate, perTrack), sr(sampleRate), ladder(slots) {
    const size_t s = size_t(slots);
    selfDecayE.assign(size_t(SynthEngines::count()), 0);
    for (const auto& e : SynthEngines::all())
        if (e.id >= 0 && e.id < SynthEngines::count()) selfDecayE[size_t(e.id)] = e.decay ? 1 : 0;
    for (auto* p : {&active, &fading, &own, &released, &triggered, &pActive, &pOwn, &pKeep, &selfDecay, &keep})
        p->assign(s, 0);
    for (auto* p : {&trackId, &note, &serial, &ftype, &quiet, &pNote}) p->assign(s, 0);
    for (auto* p : {&liveId, &engine, &pEngine, &pLive}) p->assign(s, -1);
    for (auto* p : {&start, &holdEnd, &stopAt, &pStart, &pGate, &busyUntil}) p->assign(s, 0);
    fadeAt.assign(s, forever);
    for (auto* p : {&attackF, &decaySec, &hz, &qDb}) p->assign(s, 0);
    for (auto* p : {&decayK, &env, &env0, &fadeK, &fadeG, &level, &pan}) p->assign(s, 0);
    send.assign(s * 3, 0);
    coef.assign(s, Biquad());
    z.assign(s * 4, 0);
    zR.assign(s * 4, 0);
    fdip.assign(s, 1);
    lockMask.assign(s, 0);
    pMask.assign(s, 0);
    lockVal.assign(s * VoiceParam::count, 0);
    pVal.assign(s * VoiceParam::count, 0);
    next.assign(size_t(tracks), 0);
    drift.assign(size_t(tracks), JamboDrift());
    outBuf.assign(size_t(chunk * tracks), 0);
    outBufR.assign(size_t(chunk * tracks), 0);
    gBuf.assign(size_t(chunk * tracks), 0);
    jobTracks.assign(size_t(tracks), 0);
}

int SynthVoices::activeCount() const {
    int n = 0;
    for (int v = 0; v < slots; ++v)
        if (active[size_t(v)] != 0 || pActive[size_t(v)] != 0) ++n;
    return n;
}

// MARK: — запуск

double SynthVoices::pv(const SoundData& d, int t, int p, VoiceMask mask, const float* vals) const {
    double x = double(d.param(t, p));
    if ((mask & (VoiceMask(1) << p)) != 0 && vals) x = double(vals[p]);
    // LFO/ENV — сверху (ENV — на кадр ноты).
    if (mod && mod->modulated(t)) x = mod->value(t, p, x, modFrame);
    return x;
}

void SynthVoices::trigger(const SoundData& d, int t, int e, int64_t frame, const int16_t* notes, int count,
                          VoiceMask mask, const float* vals, int64_t gate, bool isOwn, int live, bool retrig) {
    if (t < 0 || t >= tracks) return;
    modFrame = frame;
    const int pc = VoiceParam::count;
    // Ударные, SUB и TEXTURE монофоничны: ноты дорожки — в первый голос, из аккорда — нижняя.
    if (J::mono(e)) {
        if (count <= 0) return;
        const int v = t * perTrack;
        int low = int(notes[0]);
        for (int k = 1; k < std::max(1, count); ++k)
            if (int(notes[k]) < low) low = int(notes[k]);
        // Голос звучит: новая нота — в свой кадр, прежняя играет до него.
        if (active[size_t(v)] != 0 && fading[size_t(v)] == 0) {
            pActive[size_t(v)] = 1;
            pKeep[size_t(v)] = 1;
            pStart[size_t(v)] = frame;
            pNote[size_t(v)] = low;
            pEngine[size_t(v)] = e;
            pGate[size_t(v)] = gate;
            pOwn[size_t(v)] = isOwn ? 1 : 0;
            pLive[size_t(v)] = live;
            pMask[size_t(v)] = mask;
            for (int p = 0; p < pc; ++p) pVal[size_t(v * pc + p)] = vals ? vals[p] : 0;
            return;
        }
        pActive[size_t(v)] = 0;
        pKeep[size_t(v)] = 0;
        setup(v, d, t, e, frame, low, gate, mask, vals, isOwn, live, false);
        return;
    }
    const double attack = pv(d, t, VoiceParam::attack, mask, vals) / 1000;
    if (!retrig) legatoCut(t, notes, count, frame, attack);
    for (int k = 0; k < count; ++k) {
        const int n = int(notes[k]);
        const Slot sl = allocate(t, frame);
        const int v = sl.v;
        if (sl.steal) {
            // Кража: прежняя нота гаснет (τ 2,5 мс), новая — после неё (18 мс).
            fade(v, frame, 0.0025);
            pActive[size_t(v)] = 1;
            pStart[size_t(v)] = frame + int64_t(std::llround(0.018 * sr));
            pNote[size_t(v)] = n;
            pEngine[size_t(v)] = e;
            pGate[size_t(v)] = gate;
            pOwn[size_t(v)] = isOwn ? 1 : 0;
            pLive[size_t(v)] = live;
            pMask[size_t(v)] = mask;
            for (int p = 0; p < pc; ++p) pVal[size_t(v * pc + p)] = vals ? vals[p] : 0;
            stopAt[size_t(v)] = std::min(stopAt[size_t(v)], pStart[size_t(v)]);
        } else {
            pActive[size_t(v)] = 0;
            setup(v, d, t, e, frame, n, gate, mask, vals, isOwn, live, false);
        }
    }
}

void SynthVoices::setup(int v, const SoundData& d, int t, int e, int64_t frame, int n, int64_t gate, VoiceMask mask,
                        const float* vals, bool isOwn, int live, bool steal) {
    const size_t i = size_t(v);
    const int pc = VoiceParam::count;
    const bool decaying = e >= 0 && e < int(selfDecayE.size()) && selfDecayE[size_t(e)] != 0;
    const bool wasActive = active[i] != 0;
    // SUB: новая нота на звучащей — атака с текущего уровня огибающей.
    const bool legato = wasActive && !decaying && J::mono(e) && fading[i] == 0;
    const float prevEnv = legato ? env[i] * fadeG[i] : 0;
    const bool keepEnv = decaying && !steal && pv(d, t, VoiceParam::attack, mask, vals) <= 2.5;
    if (!wasActive) bank.jamboSilent(v);
    serial[i] = serialCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    active[i] = 1;
    fading[i] = 0;
    own[i] = isOwn ? 1 : 0;
    liveId[i] = live;
    trackId[i] = d.trackId[size_t(t)];
    note[i] = n;
    engine[i] = e;
    start[i] = frame;
    triggered[i] = 0;
    quiet[i] = 0;
    lockMask[i] = mask;
    for (int p = 0; p < pc; ++p) lockVal[size_t(v * pc + p)] = vals ? vals[p] : 0;
    const double attack = pv(d, t, VoiceParam::attack, mask, vals) / 1000;
    const double decay = pv(d, t, VoiceParam::decay, mask, vals) / 1000;
    const double att = std::max(0.002, attack) * sr;
    attackF[i] = att;
    decaySec[i] = decay;
    decayK[i] = float(std::exp(-1 / (std::max(0.005, decay / 4) * sr)));
    selfDecay[i] = decaying ? 1 : 0;
    keep[i] = keepEnv ? 1 : 0;
    if (decaying) {
        // Удержания и релиза нет: голос — до тишины, не дольше 15 с.
        holdEnd[i] = frame + int64_t(std::llround(att));
        stopAt[i] = holdEnd[i] + int64_t(15 * sr);
        busyUntil[i] = holdEnd[i] + int64_t((0.3 + 4 * decay) * sr);
    } else if (gate == forever) {
        busyUntil[i] = forever;
        holdEnd[i] = forever;
        stopAt[i] = forever;
    } else {
        busyUntil[i] = forever;
        const double hold = gate < 0 ? att : std::max(att, double(gate));
        holdEnd[i] = frame + int64_t(std::llround(hold));
        stopAt[i] = holdEnd[i] + int64_t((std::max(0.02, decay) * 2 + 0.05) * sr);
    }
    released[i] = 0;
    env[i] = prevEnv;
    env0[i] = prevEnv;
    fadeAt[i] = forever;
    fadeG[i] = 1;
    fadeK[i] = 1;
    // keep на звучащем хвосте и SUB поверх звучащей ноты: Level, Pan и фильтр не прыгают.
    if ((keepEnv || legato) && wasActive) return;
    level[i] = float(std::pow(pv(d, t, VoiceParam::level, mask, vals) / 127, 1.5));
    pan[i] = float(std::max(-1.0, std::min(1.0, pv(d, t, VoiceParam::pan, mask, vals) / 64)));
    for (int k = 0; k < 3; ++k) send[size_t(v * 3 + k)] = float(pv(d, t, VoiceParam::reverb + k, mask, vals) / 100);
    const Biquad::Knob f = Biquad::knob(pv(d, t, VoiceParam::filter, mask, vals), pv(d, t, VoiceParam::reso, mask, vals),
                                        pv(d, t, VoiceParam::ftype, mask, vals), e == J::sub);
    ladder.reset(v);
    ftype[i] = f.type;
    hz[i] = f.hz;
    qDb[i] = f.q;
    coef[i].set(f.type, f.hz, f.q, sr);
    fdip[i] = 1;
    for (int k = 0; k < 4; ++k) {
        z[size_t(v * 4 + k)] = 0;
        zR[size_t(v * 4 + k)] = 0;
    }
}

void SynthVoices::activatePending(int v, const SoundData& d) {
    const size_t i = size_t(v);
    pActive[i] = 0;
    const int t = v / perTrack;
    // Монофоничный: прежняя нота звучит до этого кадра — продолжение.
    const bool keepOld = pKeep[i] != 0;
    pKeep[i] = 0;
    if (!keepOld) active[i] = 0;
    if (t >= d.trackCount) return;
    setup(v, d, t, pEngine[i], pStart[i], pNote[i], pGate[i], pMask[i], &pVal[size_t(v * VoiceParam::count)],
          pOwn[i] != 0, pLive[i], !keepOld);
}

void SynthVoices::fade(int v, int64_t at, double tau) {
    const size_t i = size_t(v);
    if (active[i] == 0) return;
    const float k = float(std::exp(-1 / (tau * sr)));
    // Уже гаснет — быстрое гашение кражи перекрывает медленное.
    if (fading[i] != 0 && fadeAt[i] <= at) {
        if (k < fadeK[i]) fadeK[i] = k;
        stopAt[i] = std::min(stopAt[i], at + int64_t((tau * 8 + 0.005) * sr));
        return;
    }
    fading[i] = 1;
    fadeAt[i] = at;
    fadeK[i] = k;
    stopAt[i] = std::min(stopAt[i], at + int64_t((tau * 8 + 0.005) * sr));
}

// MARK: — звук

void SynthVoices::render(const SoundData& d, int64_t blockStart, int n, float* busL, float* busR, float* sL,
                         float* sR, uint8_t* used, int st) {
    sendL = sL;
    sendR = sR;
    stride = st;
    blockStart_ = blockStart;
    for (auto& dr : drift) dr.begin(n, sr);
    bank.granularBegin(n, sr, active.data());
    int jt = 0;
    for (int t = 0; t < tracks; ++t) {
        const int base = t * perTrack;
        bool any = false;
        for (int v = base; v < base + perTrack; ++v) {
            if (active[size_t(v)] != 0 || pActive[size_t(v)] != 0) {
                if (t >= d.trackCount) {
                    active[size_t(v)] = 0;
                    pActive[size_t(v)] = 0;
                } else {
                    any = true;
                }
            }
        }
        if (any) {
            if (used[t] == 0) {
                std::fill(busL + t * st, busL + t * st + n, 0.f);
                std::fill(busR + t * st, busR + t * st + n, 0.f);
                for (int k = 0; k < 3; ++k) {
                    std::fill(sL + (t * 3 + k) * st, sL + (t * 3 + k) * st + n, 0.f);
                    std::fill(sR + (t * 3 + k) * st, sR + (t * 3 + k) * st + n, 0.f);
                }
                used[t] = 1;
            }
            jobTracks[size_t(jt)] = t;
            jt += 1;
        }
    }
    for (int j = 0; j < jt; ++j) renderTrack(jobTracks[size_t(j)], d, n, busL, busR);
    for (auto& dr : drift) dr.end(n);
}

void SynthVoices::renderTrack(int t, const SoundData& d, int n, float* busL, float* busR) {
    const int base = t * perTrack;
    for (int v = base; v < base + perTrack; ++v)
        if (active[size_t(v)] != 0 || pActive[size_t(v)] != 0)
            renderSlot(v, d, t, blockStart_, n, busL + t * stride, busR + t * stride);
}

void SynthVoices::renderSlot(int v, const SoundData& d, int t, int64_t blockStart, int n, float* bl, float* br) {
    const size_t s = size_t(v);
    int i = 0;
    while (i < n) {
        const int64_t frame0 = blockStart + i;
        if (pActive[s] != 0 && pStart[s] <= frame0) activatePending(v, d);
        if (active[s] == 0) {
            if (pActive[s] == 0) return;
            const int st = int(pStart[s] - blockStart);
            if (st >= n) return;
            i = std::max(i + 1, st);
            continue;
        }
        if (start[s] > frame0) {
            const int st = int(start[s] - blockStart);
            if (st >= n) return;
            i = st;
            continue;
        }
        int m = std::min(chunk, n - i);
        if (pActive[s] != 0 && pStart[s] < frame0 + m) m = int(pStart[s] - frame0);
        if (m <= 0) continue;
        renderChunk(v, d, t, frame0, m, bl + i, br + i, i);
        i += m;
    }
}

double SynthVoices::knob(const float* row, const float* locks, VoiceMask mask, const ModEngine* modE, int t,
                         int64_t frame, int p) const {
    const double x = double((mask & (VoiceMask(1) << p)) != 0 ? locks[p] : row[p]);
    if (!modE) return x;
    return modE->value(t, p, x, frame);
}

void SynthVoices::renderChunk(int v, const SoundData& d, int t, int64_t frame0, int m, float* bl, float* br, int off) {
    const size_t i = size_t(v);
    const VoiceMask mask = lockMask[i];
    float* ob = outBuf.data() + t * chunk;
    float* obR = outBufR.data() + t * chunk;
    float* gb = gBuf.data() + t * chunk;
    // Ручки дорожки меняют звучащую ноту сразу; p-lock шага держит своё.
    const ModEngine* modE = mod && mod->modulated(t) ? mod : nullptr;
    const float* row = d.params.data() + d.s(t) * VoiceParam::count;
    const float* locks = lockVal.data() + v * VoiceParam::count;
    const int e = engine[i];
    float peak = 0;
    // Свои движки: p1–p4 — ручки 1.1–1.4 / 100, Decay — мс.
    double q1 = knob(row, locks, mask, modE, t, frame0, VoiceParam::harm) / 100;
    const double q2 = knob(row, locks, mask, modE, t, frame0, VoiceParam::timbre) / 100;
    const double q3 = knob(row, locks, mask, modE, t, frame0, VoiceParam::morph) / 100;
    const double q4k = knob(row, locks, mask, modE, t, frame0, VoiceParam::p4) / 100;
    // Тип фильтра — значение звука, без LFO/ENV; LADDER — только у SUB, GRIT тогда — перегруз входа фильтра.
    const VoiceMask bitF = VoiceMask(1) << VoiceParam::ftype;
    const float fty = (mask & bitF) != 0 ? locks[VoiceParam::ftype] : row[VoiceParam::ftype];
    const bool ladOK = e == J::sub;
    const double q4 = ladOK && fty > 2.5f ? 0 : q4k;
    const double dec = knob(row, locks, mask, modE, t, frame0, VoiceParam::decay);
    const double nt = double(note[i]);
    // Слежение ударных за нотами.
    if (d.pitchKey[size_t(d.s(t))] != 0 && PitchKey::drum(e)) q1 = PitchKey::knob(e, q1, nt);
    const JamboDrift& dr = drift[size_t(t)];
    if (e == J::analog) {
        const VoiceMask bm = VoiceMask(1) << VoiceParam::amod, bo = VoiceMask(1) << VoiceParam::aoct;
        const float am = (mask & bm) != 0 ? locks[VoiceParam::amod] : row[VoiceParam::amod];
        const float ao = (mask & bo) != 0 ? locks[VoiceParam::aoct] : row[VoiceParam::aoct];
        const int mode = am < 0.5f ? 0 : std::min(4, int(am + 0.5f));
        bank.jamboAnalog(v, mode, ao >= 0.5f);
    }
    if (triggered[i] == 0) {
        bank.jamboTrigger(v, e, nt, q1, q2, q3, q4, dec, double(frame0), dr.tau + double(frame0 - blockStart_) * dr.ratio);
        triggered[i] = 1;
    } else {
        bank.jamboSet(v, nt, q1, q2, q3, q4, dec);
    }
    const bool stereo = J::stereo(e);
    if (stereo) {
        // Окно активности TEXTURE и GRANULAR: до attack + 1,7·Decay после отпускания.
        const int64_t hEnd = holdEnd[i];
        int win = std::numeric_limits<int>::max();
        if (hEnd != forever) {
            const int64_t endF = hEnd + int64_t(1.7 * decaySec[i] * sr);
            win = endF > frame0 ? int(endF - frame0) : 0;
        }
        bank.stereoRender(v, ob, obR, m, win, d.grainRef[size_t(d.s(t))]);
    } else {
        bank.jamboRender(v, ob, m, dr.ratio, d.tableRef[size_t(d.s(t))]);
    }
    for (int q = 0; q < m; ++q) {
        const float a = ob[q] < 0 ? -ob[q] : ob[q];
        if (a > peak) peak = a;
        if (stereo) {
            const float b = obR[q] < 0 ? -obR[q] : obR[q];
            if (b > peak) peak = b;
        }
    }
    quiet[i] = peak < 1e-5f ? quiet[i] + m : 0;
    if (quiet[i] > silence && frame0 - start[i] > int64_t(silence)) {
        active[i] = 0;
        return;
    }
    // Сглаживание ручек (10 мс, фильтр — 15 мс).
    const float levelT = float(std::pow(knob(row, locks, mask, modE, t, frame0, VoiceParam::level) / 127, 1.5));
    const float panT =
        float(std::max(-1.0, std::min(1.0, knob(row, locks, mask, modE, t, frame0, VoiceParam::pan) / 64)));
    const Biquad::Knob fk = Biquad::knob(knob(row, locks, mask, modE, t, frame0, VoiceParam::filter),
                                         knob(row, locks, mask, modE, t, frame0, VoiceParam::reso), double(fty), ladOK);
    // keep: ручки слота на звучащем хвосте подстраиваются за ~1 мс (первые 10 мс ноты).
    const bool fast = keep[i] != 0 && frame0 - start[i] < int64_t(0.01 * sr);
    const double ks = 1 - std::exp(-double(m) / ((fast ? 0.001 : 0.01) * sr));
    const double kf = 1 - std::exp(-double(m) / ((fast ? 0.001 : 0.015) * sr));
    level[i] += (levelT - level[i]) * float(ks);
    pan[i] += (panT - pan[i]) * float(ks);
    const size_t sb = i * 3;
    send[sb] += (float(knob(row, locks, mask, modE, t, frame0, VoiceParam::reverb) / 100) - send[sb]) * float(ks);
    send[sb + 1] += (float(knob(row, locks, mask, modE, t, frame0, VoiceParam::delay) / 100) - send[sb + 1]) * float(ks);
    send[sb + 2] += (float(knob(row, locks, mask, modE, t, frame0, VoiceParam::drive) / 100) - send[sb + 2]) * float(ks);
    const float sg0 = send[sb], sg1 = send[sb + 1], sg2 = send[sb + 2];
    const bool hasSend = sg0 > 1e-6f || sg1 > 1e-6f || sg2 > 1e-6f;
    const int st = stride;
    float* s0L = sendL + t * 3 * st + off;
    float* s0R = sendR + t * 3 * st + off;
    const double h0 = hz[i];
    const double q0 = qDb[i];
    // Частота — плавно; скачок — выход на сухой сигнал за 20 мс (с HP — 80 мс), фильтр сразу на цель, обратно.
    const float g0 = fdip[i];
    Biquad cPrev = coef[i];
    const bool swap = Biquad::glide(ftype[i], hz[i], fk.type, fk.hz, kf);
    const float dg = float(double(m) / (Biquad::fadeTime * sr));
    const float dgOut = ftype[i] == 1 ? float(double(m) / (Biquad::fadeHpTime * sr)) : dg;
    bool retype = false;
    if (swap && g0 <= 0) {
        ftype[i] = fk.type;
        hz[i] = fk.hz;
        for (int k = 0; k < 4; ++k) {
            z[size_t(v * 4 + k)] = 0;
            zR[size_t(v * 4 + k)] = 0;
        }
        ladder.reset(v);
        retype = true;
    }
    fdip[i] = swap && !retype ? std::max(0.f, g0 - dgOut) : std::min(1.f, g0 + dg);
    qDb[i] += (fk.q - qDb[i]) * kf;
    const double dh = hz[i] - h0, dq = qDb[i] - q0;
    if (retype || dh > 1e-6 || dh < -1e-6 || dq > 1e-6 || dq < -1e-6) coef[i].set(ftype[i], hz[i], qDb[i], sr);
    Biquad c = coef[i];
    if (retype) cPrev = c;
    float fq = retype ? 0 : Biquad::mix(g0);
    const float dfq = (Biquad::mix(fdip[i]) - fq) / float(m);
    // LADDER (моно SUB): до огибающей, на месте в outBuf.
    if (ftype[i] == 3 && !stereo) {
        ladder.run(v, ob, m, retype ? hz[i] : h0, hz[i], qDb[i], q4k, sr, fq, dfq);
        c = Biquad();
        cPrev = c;
    }
    const float lv = level[i];
    const size_t zb = i * 4;
    const bool stopped = envelope(v, frame0, m, lv);
    // Коэффициенты фильтра — плавно от прежних к новым внутри куска.
    float b0 = cPrev.b0, b1 = cPrev.b1, b2 = cPrev.b2, a1 = cPrev.a1, a2 = cPrev.a2;
    const float im = 1 / float(m);
    const float db0 = (c.b0 - b0) * im, db1 = (c.b1 - b1) * im, db2 = (c.b2 - b2) * im;
    const float da1 = (c.a1 - a1) * im, da2 = (c.a2 - a2) * im;
    float x1 = z[zb], x2 = z[zb + 1], y1 = z[zb + 2], y2 = z[zb + 3];
    if (!stereo) {
        // Панорама, моно: cos/sin.
        const float x = (pan[i] + 1) / 2;
        const float gc = std::cos(x * fpi / 2);
        const float gs = std::sin(x * fpi / 2);
        for (int j = 0; j < m; ++j) {
            const float a = ob[j] * gb[j];
            b0 += db0;
            b1 += db1;
            b2 += db2;
            a1 += da1;
            a2 += da2;
            const float yf = b0 * a + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = a;
            y2 = y1;
            y1 = yf;
            fq += dfq;
            const float y = a + (yf - a) * fq;
            const float oa = y * gc;
            const float obv = y * gs;
            bl[j] += oa;
            br[j] += obv;
            if (hasSend) {
                s0L[j] += oa * sg0;
                s0R[j] += obv * sg0;
                s0L[st + j] += oa * sg1;
                s0R[st + j] += obv * sg1;
                s0L[2 * st + j] += oa * sg2;
                s0R[2 * st + j] += obv * sg2;
            }
        }
    } else {
        // Панорама, стерео-вход (баланс).
        const float p = pan[i];
        const float x = p <= 0 ? p + 1 : p;
        const float gc = std::cos(x * fpi / 2);
        const float gs = std::sin(x * fpi / 2);
        const float ll = p <= 0 ? 1 : gc, rl = p <= 0 ? gc : 0;
        const float rr = p <= 0 ? gs : 1, lr = p <= 0 ? 0 : gs;
        float u1 = zR[zb], u2 = zR[zb + 1], w1 = zR[zb + 2], w2 = zR[zb + 3];
        for (int j = 0; j < m; ++j) {
            const float g = gb[j];
            const float a = ob[j] * g;
            const float aR = obR[j] * g;
            b0 += db0;
            b1 += db1;
            b2 += db2;
            a1 += da1;
            a2 += da2;
            const float yf = b0 * a + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = a;
            y2 = y1;
            y1 = yf;
            const float yfR = b0 * aR + b1 * u1 + b2 * u2 - a1 * w1 - a2 * w2;
            u2 = u1;
            u1 = aR;
            w2 = w1;
            w1 = yfR;
            fq += dfq;
            const float y = a + (yf - a) * fq;
            const float yR = aR + (yfR - aR) * fq;
            const float oa = y * ll + yR * rl;
            const float obv = yR * rr + y * lr;
            bl[j] += oa;
            br[j] += obv;
            if (hasSend) {
                s0L[j] += oa * sg0;
                s0R[j] += obv * sg0;
                s0L[st + j] += oa * sg1;
                s0R[st + j] += obv * sg1;
                s0L[2 * st + j] += oa * sg2;
                s0R[2 * st + j] += obv * sg2;
            }
        }
        zR[zb] = u1;
        zR[zb + 1] = u2;
        zR[zb + 2] = (w1 < 1e-20f && w1 > -1e-20f) ? 0 : w1;
        zR[zb + 3] = (w2 < 1e-20f && w2 > -1e-20f) ? 0 : w2;
    }
    z[zb] = x1;
    z[zb + 1] = x2;
    z[zb + 2] = y1;
    z[zb + 3] = y2;
    if (stopped) {
        active[i] = 0;
        return;
    }
    if (y1 < 1e-20f && y1 > -1e-20f) z[zb + 2] = 0;
    if (y2 < 1e-20f && y2 > -1e-20f) z[zb + 3] = 0;
}

}  // namespace jambo
