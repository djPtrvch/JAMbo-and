// Свёртка комнаты и её место в колбэке (порт Core/Monitor/RoomConv.swift).
#include "RoomConv.h"
#include <algorithm>
#include <cmath>

namespace jambo {

RoomConv::RoomConv(const RoomIR& ir, const RoomIR* hd, double from, double stp)
    : out(4 * part, 0.f), name(ir.name), fft(size) {
    const int p = part, b = bins;
    parts = std::max(1, (ir.frames + p - 1) / p);
    directParts = std::max(1, std::min(parts, (ir.direct + p - 1) / p));
    hr.assign(size_t(4 * parts * b), 0.f);
    hi.assign(size_t(4 * parts * b), 0.f);
    xr.assign(size_t(2 * parts * b), 0.f);
    xi.assign(size_t(2 * parts * b), 0.f);
    ar.assign(size_t(4 * b), 0.f);
    ai.assign(size_t(4 * b), 0.f);
    inL.assign(size, 0.f);
    inR.assign(size, 0.f);
    tmp.assign(size, 0.f);
    headCount = hd ? hd->frames / p : 0;
    headFrom = float(from);
    headStep = float(stp > 0 ? stp : 2);
    const size_t hs = size_t(std::max(1, headCount * 4 * b));
    headR.assign(hs, 0.f);
    headI.assign(hs, 0.f);
    hpR.assign(size_t(4 * b), 0.f);
    hpI.assign(size_t(4 * b), 0.f);
    hnR.assign(size_t(4 * b), 0.f);
    hnI.assign(size_t(4 * b), 0.f);
    aoR.assign(size_t(2 * b), 0.f);
    aoI.assign(size_t(2 * b), 0.f);
    const float scale = 1 / fft.gain();
    for (int path = 0; path < 4; ++path) {
        for (int k = 0; k < parts; ++k) {
            std::fill(tmp.begin(), tmp.end(), 0.f);
            const int f0 = k * p;
            const int m = std::min(p, ir.frames - f0);
            for (int i = 0; i < m; ++i) tmp[size_t(i)] = ir.h[size_t(path * ir.frames + f0 + i)] * scale;
            const int at = (path * parts + k) * b;
            fft.forward(tmp.data(), hr.data() + at, hi.data() + at);
        }
    }
    if (hd) {
        const int hf = hd->frames;
        for (int set = 0; set < headCount; ++set) {
            for (int path = 0; path < 4; ++path) {
                std::fill(tmp.begin(), tmp.end(), 0.f);
                for (int i = 0; i < p; ++i) tmp[size_t(i)] = hd->h[size_t(path * hf + set * p + i)] * scale;
                const int at = (set * 4 + path) * b;
                fft.forward(tmp.data(), headR.data() + at, headI.data() + at);
            }
        }
    }
}

void RoomConv::setHighpass(double hz, int order, double sampleRate) {
    hpN = 0;
    if (hz <= 0 || order < 2) return;
    const double q4[2] = {0.5411961, 1.3065630};
    const double q2[1] = {0.7071068};
    const double* qs = order >= 4 ? q4 : q2;
    const int nq = order >= 4 ? 2 : 1;
    const double w = 2 * M_PI * hz / sampleRate;
    const double cw = std::cos(w);
    for (int k = 0; k < nq; ++k) {
        const double al = std::sin(w) / (2 * qs[k]);
        const double a0 = 1 + al;
        double* c = hpc + 5 * k;
        c[0] = (1 + cw) / 2 / a0;
        c[1] = -(1 + cw) / a0;
        c[2] = c[0];
        c[3] = -2 * cw / a0;
        c[4] = (1 - al) / a0;
    }
    std::fill(hps, hps + 8, 0.0);
    hpN = nq;
}

bool RoomConv::takeState(const RoomConv& o) {
    if (o.parts != parts || o.headCount != headCount) return false;
    xr = o.xr;
    xi = o.xi;
    head = o.head;
    inL = o.inL;
    inR = o.inR;
    out = o.out;
    hpR = o.hpR;
    hpI = o.hpI;
    headActive = o.headActive;
    if (o.hpN == hpN) std::copy(o.hps, o.hps + 8, hps);
    return true;
}

void RoomConv::feed(int k, float l, float r) {
    if (hpN == 0) {
        inL[size_t(k)] = l;
        inR[size_t(k)] = r;
        return;
    }
    for (int ch = 0; ch < 2; ++ch) {
        double x = double(ch == 0 ? l : r);
        for (int s = 0; s < hpN; ++s) {
            const double* c = hpc + 5 * s;
            double* z = hps + 4 * ch + 2 * s;
            const double y = c[0] * x + z[0];
            double z1 = c[1] * x - c[3] * y + z[1];
            double z2 = c[2] * x - c[4] * y;
            if (std::abs(z1) < 1e-25) z1 = 0;
            if (std::abs(z2) < 1e-25) z2 = 0;
            z[0] = z1;
            z[1] = z2;
            x = y;
        }
        if (ch == 0)
            inL[size_t(k)] = float(x);
        else
            inR[size_t(k)] = float(x);
    }
}

void RoomConv::copyStatic(float* dr, float* di) {
    const int b = bins;
    for (int path = 0; path < 4; ++path) {
        const size_t f = size_t(path * parts * b);
        std::copy(hr.begin() + long(f), hr.begin() + long(f) + b, dr + path * b);
        std::copy(hi.begin() + long(f), hi.begin() + long(f) + b, di + path * b);
    }
}

void RoomConv::headSpectra(float yaw) {
    const int n = 4 * bins;
    const float last = float(headCount - 1);
    float x = (yaw - headFrom) / headStep;
    if (x < 0) x = 0;
    if (x > last) x = last;
    const int i0 = int(x);
    const int i1 = std::min(i0 + 1, headCount - 1);
    const float t = x - float(i0);
    const float* ar0 = headR.data() + i0 * n;
    const float* ai0 = headI.data() + i0 * n;
    const float* ar1 = headR.data() + i1 * n;
    const float* ai1 = headI.data() + i1 * n;
    for (int q = 0; q < n; ++q) {
        hnR[size_t(q)] = ar0[q] + (ar1[q] - ar0[q]) * t;
        hnI[size_t(q)] = ai0[q] + (ai1[q] - ai0[q]) * t;
    }
}

void RoomConv::step(float yaw, bool headOnFlag) {
    const int b = bins, p = part, np = parts, h = head;
    const bool useHead = headOnFlag && headCount > 0;
    const bool mix = useHead || headActive;
    if (mix) {
        if (useHead)
            headSpectra(yaw);
        else
            copyStatic(hnR.data(), hnI.data());
        if (!headActive) copyStatic(hpR.data(), hpI.data());
    }
    fft.forward(inL.data(), xr.data() + h * b, xi.data() + h * b);
    fft.forward(inR.data(), xr.data() + (np + h) * b, xi.data() + (np + h) * b);
    std::fill(ar.begin(), ar.end(), 0.f);
    std::fill(ai.begin(), ai.end(), 0.f);
    for (int k = 0; k < np; ++k) {
        const int slot = (h - k + np) % np;
        const int acc = k < directParts ? 0 : 2;
        for (int path = 0; path < 4; ++path) {
            const int ch = path < 2 ? 0 : 1;
            const int ear = path & 1;
            const int x = (ch * np + slot) * b;
            const int a = (acc + ear) * b;
            if (mix && k == 0) {
                const int f = path * b;
                cmac(xr.data() + x, xi.data() + x, hnR.data() + f, hnI.data() + f, ar.data() + a, ai.data() + a, b);
            } else {
                const int f = (path * np + k) * b;
                cmac(xr.data() + x, xi.data() + x, hr.data() + f, hi.data() + f, ar.data() + a, ai.data() + a, b);
            }
        }
    }
    for (int j = 0; j < 4; ++j) {
        fft.inverse(ar.data() + j * b, ai.data() + j * b, tmp.data());
        std::copy(tmp.begin() + p, tmp.begin() + 2 * p, out.begin() + j * p);
    }
    if (mix) blendDirect(h);
    headActive = useHead;
    std::copy(inL.begin() + p, inL.begin() + 2 * p, inL.begin());
    std::copy(inR.begin() + p, inR.begin() + 2 * p, inR.begin());
    head = (h + 1) % np;
}

void RoomConv::blendDirect(int h) {
    const int b = bins, p = part, np = parts;
    std::fill(aoR.begin(), aoR.end(), 0.f);
    std::fill(aoI.begin(), aoI.end(), 0.f);
    for (int path = 0; path < 4; ++path) {
        const int ch = path < 2 ? 0 : 1;
        const int ear = path & 1;
        const int x = (ch * np + h) * b;
        cmac(xr.data() + x, xi.data() + x, hpR.data() + path * b, hpI.data() + path * b, aoR.data() + ear * b,
             aoI.data() + ear * b, b);
    }
    const float inv = 1 / float(p);
    for (int ear = 0; ear < 2; ++ear) {
        fft.inverse(aoR.data() + ear * b, aoI.data() + ear * b, tmp.data());
        float* o = out.data() + ear * p;
        for (int i = 0; i < p; ++i) {
            const float old = tmp[size_t(p + i)];
            const float w = float(i + 1) * inv;
            o[i] = old + (o[i] - old) * w;
        }
    }
    hpR = hnR;
    hpI = hnI;
}

void RoomConv::cmac(const float* xr, const float* xi, const float* hr, const float* hi, float* ar, float* ai, int n) {
    for (int i = 0; i < n; ++i) {
        const float r = xr[i], m = xi[i], c = hr[i], d = hi[i];
        ar[i] += r * c - m * d;
        ai[i] += r * d + m * c;
    }
}

RoomStage::RoomStage(double sampleRate) : glide(float(1 - std::exp(-1 / (0.02 * sampleRate)))) {}

RoomStage::~RoomStage() {
    while (auto it = incoming.pop()) delete *it;
    drain();
    delete cur;
    delete next;
    delete pending;
}

void RoomStage::set(std::unique_ptr<RoomConv> conv) {
    RoomConv* u = conv.release();
    if (!incoming.push(u)) delete u;
}

void RoomStage::drain() {
    while (auto r = retired.pop()) delete *r;
}

void RoomStage::process(double* l, double* r, int n) {
    while (auto item = incoming.pop()) {
        if (hasPending) retire(pending);
        pending = *item;
        hasPending = true;
    }
    if (!fading && hasPending) {
        hasPending = false;
        if (pending == nullptr && cur == nullptr) {
            pending = nullptr;
        } else {
            if (!cur) amb = ambience.load();
            next = pending;
            pending = nullptr;
            if (cur && next) next->takeState(*cur);
            fading = true;
            fade = 0;
        }
    }
    if (!cur && !fading) return;
    const float target = ambience.load();
    const int p = RoomConv::part;
    const float step = 1 / fadeFrames;
    int i = 0;
    while (i < n) {
        const int m = std::min(p - fill, n - i);
        RoomConv* a = cur;
        RoomConv* b = fading ? next : nullptr;
        for (int j = 0; j < m; ++j) {
            const int k = fill + j;
            const float dl = float(l[i + j]), dr = float(r[i + j]);
            amb += (target - amb) * glide;
            float ol = dl, orr = dr;
            if (a) {
                a->feed(p + k, dl, dr);
                ol = a->out[size_t(k)] + amb * a->out[size_t(2 * p + k)];
                orr = a->out[size_t(p + k)] + amb * a->out[size_t(3 * p + k)];
            }
            if (fading) {
                float nl = dl, nr = dr;
                if (b) {
                    b->feed(p + k, dl, dr);
                    nl = b->out[size_t(k)] + amb * b->out[size_t(2 * p + k)];
                    nr = b->out[size_t(p + k)] + amb * b->out[size_t(3 * p + k)];
                }
                fade = std::min(1.f, fade + step);
                ol += (nl - ol) * fade;
                orr += (nr - orr) * fade;
            }
            l[i + j] = double(ol);
            r[i + j] = double(orr);
        }
        fill += m;
        i += m;
        if (fill == p) {
            fill = 0;
            const bool on = headOn.load() > 0.5f;
            const float tgt = on ? headYaw.load() : 0;
            yaw += (tgt - yaw) * yawGlide;
            const bool use = on || yaw > 0.05f || yaw < -0.05f;
            if (a) a->step(yaw, use);
            if (b) b->step(yaw, use);
        }
        if (fading && fade >= 1) {
            fading = false;
            retire(cur);
            cur = next;
            next = nullptr;
        }
    }
}

}  // namespace jambo
