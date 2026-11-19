// Микшер дорожек (порт Core/Sampler/SamplerMix.swift): громкость, mute/solo, Sidechain, посылы; пики дорожек для метров,
// выходы дорожек (outL, outR) — источники динамических полос EQ; дорожки — в порядке SoundData::eqOrder.
#include <algorithm>
#include <cmath>
#include "Sampler.h"
#include "util/RenderProfile.h"

namespace jambo {

void Sampler::mix(const SoundData& d, int n, float* left, float* right) {
    // Гейт mute/solo — за ~5 мс (постоянная 1,5 мс), громкость — за ~10 мс, глубина Sidechain и переключатели
    // посылов — за ~10 мс (постоянная 3,3 мс).
    const float gk = float(1 - std::exp(-1 / (0.0015 * sr)));
    const float vk = float(1 - std::exp(-1 / (0.01 * sr)));
    const float dk = float(1 - std::exp(-1 / (0.0033 * sr)));
    FxRack* rack = fx;
    const float* eBuf = rack ? rack->e.data() : nullptr;
    const int source = rack ? rack->source : -1;
    // Переключатели посылов — общие для дорожек (сглаживание на блок).
    float fl0 = 0, fl1 = 0, fl2 = 0;
    if (rack) {
        for (size_t k = 0; k < 3; ++k) {
            if (flagCur[k] < 0) flagCur[k] = d.scSend[k];
            flagCur[k] += (d.scSend[k] - flagCur[k]) * std::min(1.f, dk * float(n));
        }
        fl0 = flagCur[0];
        fl1 = flagCur[1];
        fl2 = flagCur[2];
    }
    // Наибольшая огибающая Sidechain в блоке — для приглушения на метрах MIX.
    float eMax = 0;
    if (eBuf)
        for (int i = 0; i < n; ++i)
            if (eBuf[i] > eMax) eMax = eBuf[i];
    // Громкость с p-lock играющего шага MIX.
    const uint16_t lockMask = mixLockMask ? *mixLockMask : 0;
    const float* lockGain = mixLockGain;
    const int mf = maxFrames;
    // Запись стемов: гейт mute/solo по отсчётам.
    float* stemG = stemGate;
    const bool cap = stemG != nullptr;
    float* gw0 = stemG ? stemG : meterL.data();
    parallelEqs(d, n, rack != nullptr);
    lap(RenderProfile::eq);
    for (int oi = 0; oi < d.trackCount; ++oi) {
        const int t = d.eqOrder[size_t(oi)];
        const size_t T = size_t(t);
        float* ol = outL.data() + t * mf;
        float* orr = outR.data() + t * mf;
        // Громкость — клипа, который сейчас играет.
        float vt = d.gain[size_t(d.s(t))];
        if ((lockMask & (uint16_t(1) << uint16_t(t))) && lockGain) vt = lockGain[t] * d.trim[T];
        const float gt = d.open[T] != 0 ? 1 : 0;
        const float at = t == source ? 0 : std::max(0.f, std::min(1.f, d.scAmount[size_t(d.s(t))] / 100));
        if (volCur[T] < 0) volCur[T] = vt;
        if (gateCur[T] < 0) gateCur[T] = gt;
        float* meter = meterOf(t);
        const bool loud = loudOf(t);
        if (used[T] == 0) {
            volCur[T] = vt;
            gateCur[T] = gt;
            duckCur[T] = at;
            if (meter) meter[2] = at * eMax;
            std::fill(ol, ol + n, 0.f);
            std::fill(orr, orr + n, 0.f);
            lap(RenderProfile::mix);
            idleEq(t, d, n);
            lap(RenderProfile::eq);
            continue;
        }
        lap(RenderProfile::mix);
        if (eqDone[T] == 0) trackEq(t, d, n, rack != nullptr);
        lap(RenderProfile::eq);
        const float* bl = busL.data() + t * mf;
        const float* br = busR.data() + t * mf;
        float* ml = meterL.data();
        float* mr = meterR.data();
        float* gw = gw0 + (cap ? t * mf : 0);
        float vol = volCur[T], gate = gateCur[T], amt = duckCur[T];
        float pl = 0, pr = 0;
        if (rack && eBuf) {
            const float* s0L = sendL.data() + t * 3 * mf;
            const float* s0R = sendR.data() + t * 3 * mf;
            // Входы эффектов дорожки: посылы этого блока — записью, не сложением.
            float* i0L = rack->inL.data() + t * 3 * mf;
            float* i0R = rack->inR.data() + t * 3 * mf;
            rack->fed[T] = 1;
            for (int i = 0; i < n; ++i) {
                vol += (vt - vol) * vk;
                gate += (gt - gate) * gk;
                amt += (at - amt) * dk;
                const float g = vol * gate;
                const float de = amt * eBuf[i];
                // Метр — после громкости и Sidechain, до mute/solo.
                const float pvv = vol * (1 - de);
                const float xl = bl[i] * pvv;
                const float xr = br[i] * pvv;
                ol[i] = xl;
                orr[i] = xr;
                left[i] += xl * gate;
                right[i] += xr * gate;
                if (cap) gw[i] = gate;
                const float al = xl < 0 ? -xl : xl;
                const float ar = xr < 0 ? -xr : xr;
                if (al > pl) pl = al;
                if (ar > pr) pr = ar;
                if (loud) {
                    ml[i] = xl;
                    mr[i] = xr;
                }
                // Посылы Reverb, Delay, Drive.
                float gs = g * (1 - fl0 * de);
                i0L[i] = s0L[i] * gs;
                i0R[i] = s0R[i] * gs;
                gs = g * (1 - fl1 * de);
                i0L[mf + i] = s0L[mf + i] * gs;
                i0R[mf + i] = s0R[mf + i] * gs;
                gs = g * (1 - fl2 * de);
                i0L[2 * mf + i] = s0L[2 * mf + i] * gs;
                i0R[2 * mf + i] = s0R[2 * mf + i] * gs;
            }
        } else {
            for (int i = 0; i < n; ++i) {
                vol += (vt - vol) * vk;
                gate += (gt - gate) * gk;
                const float xl = bl[i] * vol;
                const float xr = br[i] * vol;
                ol[i] = xl;
                orr[i] = xr;
                left[i] += xl * gate;
                right[i] += xr * gate;
                if (cap) gw[i] = gate;
                const float al = xl < 0 ? -xl : xl;
                const float ar = xr < 0 ? -xr : xr;
                if (al > pl) pl = al;
                if (ar > pr) pr = ar;
                if (loud) {
                    ml[i] = xl;
                    mr[i] = xr;
                }
            }
        }
        volCur[T] = vol;
        gateCur[T] = gate;
        duckCur[T] = amt;
        if (meter) {
            meter[0] = pl;
            meter[1] = pr;
            meter[2] = amt * eMax;
        }
        if (loud) loudProcess(ml, mr, n);
    }
    // Съём для FREQ и DYN (MIX) — выходы дорожек этого блока.
    tapTracks(d, n);
    // Дорожек нет — выходы молчат.
    for (int t = d.trackCount; t < SoundData::maxTracks; ++t) {
        std::fill(outL.begin() + t * mf, outL.begin() + t * mf + n, 0.f);
        std::fill(outR.begin() + t * mf, outR.begin() + t * mf + n, 0.f);
    }
}

void Sampler::runJob(int j) {
    if (!eqJobData) return;
    trackEq(eqJobs[size_t(j)], *eqJobData, eqJobN, eqJobSends);
}

}  // namespace jambo
