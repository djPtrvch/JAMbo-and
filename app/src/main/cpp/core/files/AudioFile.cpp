#include "AudioFile.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace jambo {

namespace {

struct Reader {
    const uint8_t* p;
    size_t n;
    bool ok(size_t at, size_t len) const { return at <= n && len <= n - at; }
    uint32_t le32(size_t at) const { return uint32_t(p[at]) | uint32_t(p[at + 1]) << 8 | uint32_t(p[at + 2]) << 16 | uint32_t(p[at + 3]) << 24; }
    uint16_t le16(size_t at) const { return uint16_t(p[at] | p[at + 1] << 8); }
    uint32_t be32(size_t at) const { return uint32_t(p[at]) << 24 | uint32_t(p[at + 1]) << 16 | uint32_t(p[at + 2]) << 8 | p[at + 3]; }
    uint16_t be16(size_t at) const { return uint16_t(p[at] << 8 | p[at + 1]); }
    uint64_t be64(size_t at) const { return uint64_t(be32(at)) << 32 | be32(at + 4); }
    bool tag(size_t at, const char* t) const { return ok(at, 4) && std::memcmp(p + at, t, 4) == 0; }
};

enum class Fmt { intLE, intBE, floatLE, floatBE };

/// Один отсчёт: целое со знаком (8 бит в WAV — без знака) или float, к −1…1.
float sampleAt(const uint8_t* s, int bytes, Fmt f, bool unsigned8) {
    if (f == Fmt::floatLE || f == Fmt::floatBE) {
        uint8_t b[8];
        for (int i = 0; i < bytes; i++) b[i] = f == Fmt::floatLE ? s[i] : s[bytes - 1 - i];
        if (bytes == 4) {
            float v;
            std::memcpy(&v, b, 4);
            return std::isfinite(v) ? v : 0.f;
        }
        double v;
        std::memcpy(&v, b, 8);
        return std::isfinite(v) ? float(v) : 0.f;
    }
    if (bytes == 1) return unsigned8 ? (float(s[0]) - 128.f) / 128.f : float(int8_t(s[0])) / 128.f;
    int64_t v = 0;
    for (int i = 0; i < bytes; i++) {
        const uint8_t b = f == Fmt::intLE ? s[bytes - 1 - i] : s[i];
        v = (v << 8) | b;
    }
    const int bits = bytes * 8;
    if (v & (int64_t(1) << (bits - 1))) v -= int64_t(1) << bits;
    return float(double(v) / double(int64_t(1) << (bits - 1)));
}

std::optional<PcmAudio> frames(const Reader& r, size_t at, size_t len, int channels, int bytes, Fmt f, double rate,
                               bool unsigned8) {
    if (channels < 1 || bytes < 1 || bytes > 8 || rate < 1000 || rate > 768000) return std::nullopt;
    len = std::min(len, r.n - std::min(at, r.n));
    const size_t frameBytes = size_t(channels) * size_t(bytes);
    const size_t count = len / frameBytes;
    if (count == 0) return std::nullopt;
    PcmAudio a;
    a.rate = rate;
    a.left.resize(count);
    if (channels >= 2) a.right.resize(count);
    for (size_t i = 0; i < count; i++) {
        const uint8_t* s = r.p + at + i * frameBytes;
        a.left[i] = sampleAt(s, bytes, f, unsigned8);
        if (channels >= 2) a.right[i] = sampleAt(s + bytes, bytes, f, unsigned8);
    }
    return a;
}

std::optional<PcmAudio> wav(const Reader& r) {
    size_t at = 12;
    int fmt = 0, channels = 0, bits = 0;
    double rate = 0;
    bool haveFmt = false;
    while (r.ok(at, 8)) {
        const uint32_t size = r.le32(at + 4);
        const size_t body = at + 8;
        if (r.tag(at, "fmt ") && r.ok(body, 16)) {
            fmt = r.le16(body);
            channels = r.le16(body + 2);
            rate = r.le32(body + 4);
            bits = r.le16(body + 14);
            if (fmt == 0xFFFE && size >= 40 && r.ok(body, 26)) fmt = r.le16(body + 24);
            haveFmt = true;
        } else if (r.tag(at, "data") && haveFmt) {
            const int bytes = (bits + 7) / 8;
            if (fmt == 1) return frames(r, body, size, channels, bytes, Fmt::intLE, rate, bytes == 1);
            if (fmt == 3) return frames(r, body, size, channels, bytes, Fmt::floatLE, rate, false);
            return std::nullopt;
        }
        at = body + size + (size & 1);
    }
    return std::nullopt;
}

/// 80-битное расширенное число (частота AIFF).
double ext80(const Reader& r, size_t at) {
    const int e = int(r.be16(at) & 0x7FFF) - 16383 - 63;
    const uint64_t m = r.be64(at + 2);
    return std::ldexp(double(m), e);
}

std::optional<PcmAudio> aiff(const Reader& r, bool aifc) {
    size_t at = 12;
    int channels = 0, bits = 0;
    double rate = 0;
    Fmt f = Fmt::intBE;
    bool haveComm = false;
    while (r.ok(at, 8)) {
        const uint32_t size = r.be32(at + 4);
        const size_t body = at + 8;
        if (r.tag(at, "COMM") && r.ok(body, 18)) {
            channels = r.be16(body);
            bits = r.be16(body + 6);
            rate = ext80(r, body + 8);
            if (aifc && r.ok(body, 22)) {
                if (r.tag(body + 18, "sowt")) f = Fmt::intLE;
                else if (r.tag(body + 18, "fl32") || r.tag(body + 18, "FL32")) f = Fmt::floatBE, bits = 32;
                else if (r.tag(body + 18, "fl64") || r.tag(body + 18, "FL64")) f = Fmt::floatBE, bits = 64;
                else if (!r.tag(body + 18, "NONE") && !r.tag(body + 18, "twos")) return std::nullopt;
            }
            haveComm = true;
        } else if (r.tag(at, "SSND") && haveComm && r.ok(body, 8)) {
            const uint32_t offset = r.be32(body);
            const size_t start = body + 8 + offset;
            return frames(r, start, size >= 8 + offset ? size - 8 - offset : 0, channels, (bits + 7) / 8, f, rate, false);
        }
        at = body + size + (size & 1);
    }
    return std::nullopt;
}

std::optional<PcmAudio> cafFile(const Reader& r) {
    size_t at = 8;
    int channels = 0, bits = 0;
    double rate = 0;
    Fmt f = Fmt::intBE;
    bool haveDesc = false;
    while (r.ok(at, 12)) {
        const uint64_t size = r.be64(at + 4);
        const size_t body = at + 12;
        if (r.tag(at, "desc") && r.ok(body, 32)) {
            const uint64_t rb = r.be64(body);
            std::memcpy(&rate, &rb, 8);
            if (!r.tag(body + 8, "lpcm")) return std::nullopt;
            const uint32_t flags = r.be32(body + 12);
            channels = int(r.be32(body + 24));
            bits = int(r.be32(body + 28));
            const bool isFloat = flags & 1, little = flags & 2;
            f = isFloat ? (little ? Fmt::floatLE : Fmt::floatBE) : (little ? Fmt::intLE : Fmt::intBE);
            haveDesc = true;
        } else if (r.tag(at, "data") && haveDesc) {
            // Размер −1 — до конца файла; первые 4 байта — editCount.
            const size_t len = size == ~uint64_t(0) ? r.n - body : size_t(std::min<uint64_t>(size, r.n));
            if (len < 4) return std::nullopt;
            return frames(r, body + 4, len - 4, channels, (bits + 7) / 8, f, rate, false);
        }
        if (size == ~uint64_t(0) || size > r.n) break;
        at = body + size_t(size);
    }
    return std::nullopt;
}

void put32le(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back(uint8_t(v >> (8 * i))); }
void put16le(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
void put32be(std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; i--) o.push_back(uint8_t(v >> (8 * i))); }
void put64be(std::vector<uint8_t>& o, uint64_t v) { put32be(o, uint32_t(v >> 32)); put32be(o, uint32_t(v)); }
void putTag(std::vector<uint8_t>& o, const char* t) { o.insert(o.end(), t, t + 4); }

void putFloats(std::vector<uint8_t>& o, const std::vector<float>& l, const std::vector<float>* r) {
    const size_t at = o.size();
    const size_t ch = r ? 2 : 1;
    o.resize(at + l.size() * ch * 4);
    uint8_t* d = o.data() + at;
    for (size_t i = 0; i < l.size(); i++) {
        float v[2] = {l[i], r ? (i < r->size() ? (*r)[i] : 0.f) : 0.f};
        for (size_t c = 0; c < ch; c++) {
            uint32_t u;
            std::memcpy(&u, &v[c], 4);
            for (int b = 0; b < 4; b++) *d++ = uint8_t(u >> (8 * b));
        }
    }
}

}  // namespace

std::optional<PcmAudio> AudioFile::parse(const uint8_t* data, size_t n) {
    const Reader r{data, n};
    std::optional<PcmAudio> a;
    if (r.tag(0, "RIFF") && r.tag(8, "WAVE")) a = wav(r);
    else if (r.tag(0, "FORM") && r.tag(8, "AIFF")) a = aiff(r, false);
    else if (r.tag(0, "FORM") && r.tag(8, "AIFC")) a = aiff(r, true);
    else if (r.tag(0, "caff")) a = cafFile(r);
    if (a && a->frames() == 0) return std::nullopt;
    return a;
}

std::vector<uint8_t> AudioFile::caf(const std::vector<float>& left, const std::vector<float>* right, double rate) {
    const uint32_t ch = right ? 2 : 1;
    std::vector<uint8_t> o;
    putTag(o, "caff");
    o.push_back(0); o.push_back(1);  // версия 1
    o.push_back(0); o.push_back(0);  // флаги
    putTag(o, "desc");
    put64be(o, 32);
    uint64_t rb;
    std::memcpy(&rb, &rate, 8);
    put64be(o, rb);
    putTag(o, "lpcm");
    put32be(o, 1 | 2);   // float, little-endian
    put32be(o, 4 * ch);  // байт на пакет
    put32be(o, 1);       // кадров на пакет
    put32be(o, ch);
    put32be(o, 32);
    putTag(o, "data");
    put64be(o, uint64_t(left.size()) * 4 * ch + 4);
    put32be(o, 0);  // editCount
    putFloats(o, left, right);
    return o;
}

std::vector<uint8_t> AudioFile::wavFloat32(const std::vector<float>& left, const std::vector<float>* right, int rate) {
    const uint32_t ch = right ? 2 : 1;
    const uint32_t frames = uint32_t(left.size());
    const uint32_t dataBytes = frames * ch * 4;
    std::vector<uint8_t> o;
    putTag(o, "RIFF"); put32le(o, 4 + 26 + 12 + 8 + dataBytes); putTag(o, "WAVE");
    putTag(o, "fmt "); put32le(o, 18); put16le(o, 3); put16le(o, ch); put32le(o, uint32_t(rate));
    put32le(o, uint32_t(rate) * ch * 4); put16le(o, ch * 4); put16le(o, 32); put16le(o, 0);
    putTag(o, "fact"); put32le(o, 4); put32le(o, frames);
    putTag(o, "data"); put32le(o, dataBytes);
    putFloats(o, left, right);
    return o;
}

// MARK: — пересчёт частоты: окно-синк с окном Кайзера (β 9, 48 нулей с каждой стороны), фазы — таблицей с
// линейной интерполяцией; срез — 0,95 меньшей частоты Найквиста.

namespace {
constexpr int zeros = 48;
constexpr int phases = 512;
constexpr double beta = 9.0;

double bessel0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 40; k++) {
        t *= (x / (2 * k)) * (x / (2 * k));
        s += t;
        if (t < 1e-12 * s) break;
    }
    return s;
}
}  // namespace

std::vector<float> Resample::run(const std::vector<float>& x, double from, double to) {
    if (from == to || x.empty()) return x;
    const double ratio = to / from;
    const double cut = 0.95 * std::min(1.0, ratio);  // доля частоты Найквиста входа
    const int half = int(std::ceil(zeros / cut));       // половина ядра в отсчётах входа
    // Таблица ядра: t от 0 до half с шагом 1/phases.
    const int tn = half * phases + 2;
    std::vector<float> table(static_cast<size_t>(tn));
    const double i0b = bessel0(beta);
    for (int i = 0; i < tn; i++) {
        const double t = double(i) / phases;
        const double w = t >= half ? 0 : bessel0(beta * std::sqrt(1 - (t / half) * (t / half))) / i0b;
        const double a = M_PI * cut * t;
        const double s = t == 0 ? cut : cut * std::sin(a) / a;
        table[size_t(i)] = float(s * w);
    }
    auto kernel = [&](double t) {
        t = std::fabs(t) * phases;
        const int i = int(t);
        if (i + 1 >= tn) return 0.f;
        const float f = float(t - i);
        return table[size_t(i)] + (table[size_t(i + 1)] - table[size_t(i)]) * f;
    };
    const size_t outN = size_t(std::floor(double(x.size()) * ratio));
    std::vector<float> y(outN);
    const long long n = (long long)x.size();
    for (size_t k = 0; k < outN; k++) {
        const double c = double(k) / ratio;
        const long long c0 = (long long)std::floor(c);
        double s = 0;
        for (long long j = c0 - half + 1; j <= c0 + half; j++) {
            if (j < 0 || j >= n) continue;
            s += double(x[size_t(j)]) * kernel(c - double(j));
        }
        y[k] = float(s);
    }
    return y;
}

}  // namespace jambo
