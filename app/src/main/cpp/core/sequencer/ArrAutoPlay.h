// Автоматизация на таймлайне в колбэке (порт Core/Sequencer/ArrangeAutoPlay.swift, ветка swift JAMbo): каждый блок
// значение кривой по позиции курсора подставляется вместо ручки в снимок звука; прежнее значение возвращается, когда
// кривая перестала действовать. Chance и Ratchet — на момент шага (step).
#pragma once
#include <cstdint>
#include <vector>
#include "SeqData.h"
#include "model/ArrangeAuto.h"
#include "sampler/SoundSnapshot.h"

namespace jambo {

class ArrAutoPlay {
public:
    static constexpr int count = ArrAutoTarget::count;
    static constexpr int sounds = SoundData::maxTracks * 2;

    ArrAutoPlay()
        : saved(size_t(sounds * count), 0), has(size_t(sounds * count), 0), hold(size_t(SeqData::maxTracks * count), 0) {}

    void setHold(int t, int j, bool on) {
        if (t < 0 || t >= SeqData::maxTracks || j < 0 || j >= count) return;
        hold[size_t(t * count + j)] = on ? 1 : 0;
    }

    /// Блок: x — позиция курсора (шаги, дробная; −1 — транспорт стоит), follow — дорожки на таймлайне.
    void apply(const SeqData& seq, SoundData& snd, double x, const bool* follow) {
        const int n = count;
        if (snd.params.data() != lastParams) {
            // Новый снимок звука — ручки в нём свои, подставлять заново.
            std::fill(has.begin(), has.end(), 0);
            lastParams = snd.params.data();
            anyHas = false;
        }
        bool any = anyHas;
        if (!any && x >= 0)
            for (int t = 0; t < seq.trackCount; ++t)
                if (seq.autoAny[size_t(t)] != 0 && follow[t]) any = true;
        if (!any) return;
        bool still = false;
        const int vc = VoiceParam::count, mc = ModTarget::count;
        for (int u = 0; u < sounds; ++u) {
            const int t = u % SeqData::maxTracks;
            const bool on = x >= 0 && t < seq.trackCount && t < snd.trackCount && snd.s(t) == u && follow[t] &&
                            seq.autoAny[size_t(t)] != 0;
            for (int j = 0; j < ArrAutoTarget::prob; ++j) {
                const size_t c = size_t(u * n + j);
                double v = -1;
                if (on && hold[size_t(t * n + j)] == 0) v = seq.autoValue(t, j, x);
                float* cur = j < vc ? &snd.params[size_t(u * vc + j)] : &snd.scAmount[size_t(u)];
                if (v >= 0) {
                    if (has[c] == 0) {
                        saved[c] = *cur;
                        has[c] = 1;
                    }
                    const double value = j < vc ? snd.scale[size_t(u * mc + j)].fromNorm(v) : v * 100;
                    *cur = float(value);
                    still = true;
                } else if (has[c] != 0) {
                    *cur = saved[c];
                    has[c] = 0;
                }
            }
        }
        anyHas = still;
    }

    /// Chance или Ratchet (j — ArrAutoTarget::prob / ratchet) дорожки t на шаге x: значение кривой или base.
    double step(const SeqData& seq, const SoundData* snd, int t, int j, double x, double base) const {
        if (seq.autoAny[size_t(t)] == 0 || hold[size_t(t * count + j)] != 0) return base;
        if (!snd) return base;
        const double v = seq.autoValue(t, j, x);
        if (v < 0) return base;
        const int mj = j == ArrAutoTarget::prob ? ModTarget::prob : ModTarget::ratchet;
        return snd->scale[size_t(snd->s(t) * ModTarget::count + mj)].fromNorm(v);
    }

private:
    std::vector<float> saved;
    std::vector<uint8_t> has, hold;
    const float* lastParams = nullptr;
    bool anyHas = false;
};

}  // namespace jambo
