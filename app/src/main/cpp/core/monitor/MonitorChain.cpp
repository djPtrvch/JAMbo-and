// Монитор-цепочка в колбэке (порт Core/Monitor/MonitorChain.swift).
#include "MonitorChain.h"
#include <algorithm>
#include <cmath>

namespace jambo {

MonitorChain::MonitorChain(double sr)
    : sampleRate(sr), room(sr), ring(size_t(slots * MonitorSpec::size), 0.0), tgt(MonitorSpec::size, 0.0),
      cur(MonitorSpec::size, 0.0), state(size_t(MonitorSpec::sections * 4), 0.0), wl(maxFrames, 0.0),
      wr(maxFrames, 0.0), ldl(MonitorSpec::look, 0.0), ldr(MonitorSpec::look, 0.0),
      lneed(size_t(MonitorSpec::look + 1), 1.0), lbox(MonitorSpec::look, 1.0) {
    holdN = int(MonitorSpec::hold * sr);
    fadeStep = 1 / (MonitorSpec::fade * sr);
    release = 1 - std::exp(-1 / (MonitorSpec::release * sr));
    ceiling = std::pow(10.0, MonitorSpec::ceilingDb / 20);
    for (int k = 0; k < MonitorSpec::sections; ++k) {
        tgt[size_t(k * 5)] = 1;
        cur[size_t(k * 5)] = 1;
    }
    tgt[MonitorSpec::compAt] = 1;
    cur[MonitorSpec::compAt] = 1;
}

void MonitorChain::publish(const std::vector<double>& p) {
    if (int(p.size()) != MonitorSpec::size) return;
    std::copy(p.begin(), p.end(), ring.begin() + long(nextSlot) * MonitorSpec::size);
    queue.push(nextSlot);
    nextSlot = (nextSlot + 1) % slots;
}

void MonitorChain::process(float* left, float* right, int frames, bool raw) {
    int latest = -1;
    while (auto k = queue.pop()) latest = *k;
    if (latest >= 0)
        std::copy(ring.begin() + long(latest) * MonitorSpec::size,
                  ring.begin() + long(latest + 1) * MonitorSpec::size, tgt.begin());
    if (paused.load() != 0) {
        wet = 0;
        return;
    }
    const int n = std::min(frames, maxFrames);
    const double wetTarget = tgt[MonitorSpec::onAt] > 0.5 && !raw && !neutral() ? 1 : 0;
    glide(n);
    for (int i = 0; i < n; ++i) {
        wl[size_t(i)] = double(left[i]);
        wr[size_t(i)] = double(right[i]);
    }
    processMono(n);
    room.process(wl.data(), wr.data(), n);
    for (int k = 0; k < MonitorSpec::sections; ++k) section(k, n);
    processGain(n);
    output(left, right, n, wetTarget);
}

bool MonitorChain::neutral() const {
    if (tgt[MonitorSpec::compAt] != 1 || tgt[MonitorSpec::monoAt] != 0 || mono != 0 || room.active()) return false;
    for (int k = 0; k < MonitorSpec::sections; ++k) {
        const double* c = tgt.data() + k * 5;
        if (c[0] != 1 || c[1] != 0 || c[2] != 0 || c[3] != 0 || c[4] != 0) return false;
    }
    return true;
}

void MonitorChain::glide(int n) {
    const double a = 1 - std::exp(-double(n) / (MonitorSpec::glide * sampleRate));
    for (int k = 0; k < MonitorSpec::compAt; ++k) {
        const double d = tgt[size_t(k)] - cur[size_t(k)];
        if (d != 0) cur[size_t(k)] = (d < 1e-12 && d > -1e-12) ? tgt[size_t(k)] : cur[size_t(k)] + d * a;
    }
}

void MonitorChain::processMono(int n) {
    const double target = tgt[MonitorSpec::monoAt];
    double m = mono;
    if (m == 0 && target == 0) return;
    for (int i = 0; i < n; ++i) {
        if (m < target)
            m = std::min(target, m + fadeStep);
        else if (m > target)
            m = std::max(target, m - fadeStep);
        const double mid = 0.5 * (wl[size_t(i)] + wr[size_t(i)]);
        wl[size_t(i)] += (mid - wl[size_t(i)]) * m;
        wr[size_t(i)] += (mid - wr[size_t(i)]) * m;
    }
    mono = m;
}

void MonitorChain::section(int k, int n) {
    const double* c = cur.data() + k * 5;
    double* s = state.data() + k * 4;
    const double b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
    if (b0 == 1 && b1 == 0 && b2 == 0 && a1 == 0 && a2 == 0) {
        s[0] = s[1] = s[2] = s[3] = 0;
        return;
    }
    double s1l = s[0], s2l = s[1], s1r = s[2], s2r = s[3];
    for (int i = 0; i < n; ++i) {
        const double xl = wl[size_t(i)];
        const double yl = b0 * xl + s1l;
        s1l = b1 * xl - a1 * yl + s2l;
        s2l = b2 * xl - a2 * yl;
        wl[size_t(i)] = yl;
        const double xr = wr[size_t(i)];
        const double yr = b0 * xr + s1r;
        s1r = b1 * xr - a1 * yr + s2r;
        s2r = b2 * xr - a2 * yr;
        wr[size_t(i)] = yr;
    }
    if (std::abs(s1l) < 1e-30 && std::abs(s2l) < 1e-30) s1l = s2l = 0;
    if (std::abs(s1r) < 1e-30 && std::abs(s2r) < 1e-30) s1r = s2r = 0;
    s[0] = s1l;
    s[1] = s2l;
    s[2] = s1r;
    s[3] = s2r;
}

void MonitorChain::processGain(int n) {
    double* l = wl.data();
    double* r = wr.data();
    const double c0 = comp, c1 = tgt[MonitorSpec::compAt];
    if (c0 != 1 || c1 != 1) {
        const double dc = (c1 - c0) / double(n);
        for (int i = 0; i < n; ++i) {
            const double gi = c0 + dc * double(i + 1);
            l[i] *= gi;
            r[i] *= gi;
        }
    }
    comp = c1;
    const int la = MonitorSpec::look;
    const double inv = 1 / double(la);
    double lg = limit;
    int hold = holdLeft;
    int pos = lpos;
    double sum = 0;
    for (int q = 0; q < la; ++q) sum += lbox[size_t(q)];
    double least = 1;
    for (int i = 0; i < n; ++i) {
        const double xl = l[i], xr = r[i];
        const double al = xl < 0 ? -xl : xl;
        const double ar = xr < 0 ? -xr : xr;
        const double peak = al > ar ? al : ar;
        lneed[size_t(pos % (la + 1))] = peak > ceiling ? ceiling / peak : 1;
        double held = 1;
        for (int q = 0; q <= la; ++q)
            if (lneed[size_t(q)] < held) held = lneed[size_t(q)];
        if (held < lg) {
            lg = held;
            hold = holdN;
        } else if (hold > 0) {
            --hold;
        } else if (lg < held) {
            lg += (held - lg) * release;
            if (lg > held - 1e-9) lg = held;
        }
        const size_t k = size_t(pos % la);
        sum += lg - lbox[k];
        lbox[k] = lg;
        const double g = sum * inv;
        l[i] = ldl[k] * g;
        r[i] = ldr[k] * g;
        ldl[k] = xl;
        ldr[k] = xr;
        if (g < least) least = g;
        pos = (pos + 1) % (la * (la + 1));
    }
    lpos = pos;
    limit = lg;
    holdLeft = hold;
    limitGain.store(float(least));
}

void MonitorChain::output(float* left, float* right, int n, double target) {
    double w = wet;
    if (w == target && w == 0) return;
    if (w == target) {
        for (int i = 0; i < n; ++i) {
            left[i] = float(wl[size_t(i)]);
            right[i] = float(wr[size_t(i)]);
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        if (w < target)
            w = std::min(target, w + fadeStep);
        else if (w > target)
            w = std::max(target, w - fadeStep);
        const double dl = double(left[i]), dr = double(right[i]);
        left[i] = float(dl + (wl[size_t(i)] - dl) * w);
        right[i] = float(dr + (wr[size_t(i)] - dr) * w);
    }
    wet = w;
}

}  // namespace jambo
