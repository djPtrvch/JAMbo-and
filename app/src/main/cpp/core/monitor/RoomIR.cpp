// Импульсы комнат (порт Core/Monitor/RoomIR.swift).
#include "RoomIR.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include "MonitorDesign.h"
#include "sampler/PvFFT.h"

namespace jambo {

namespace {
double num(const nlohmann::json& o, const char* k, double def) {
    return o.contains(k) && o[k].is_number() ? o[k].get<double>() : def;
}
std::string str(const nlohmann::json& o, const char* k, const std::string& def) {
    return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : def;
}
}  // namespace

std::vector<std::shared_ptr<RoomInfo>> RoomInfo::list(const std::string& json) {
    std::vector<std::shared_ptr<RoomInfo>> out;
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object() || !root.contains("rooms") || !root["rooms"].is_array()) return out;
    for (const auto& r : root["rooms"]) {
        if (!r.is_object() || !r.contains("name") || !r["name"].is_string() || !r.contains("file") ||
            !r["file"].is_string())
            continue;
        auto info = std::make_shared<RoomInfo>();
        info->name = r["name"].get<std::string>();
        info->file = r["file"].get<std::string>();
        info->gainDb = num(r, "gain_db", 0);
        info->direct = r.contains("direct") && r["direct"].is_number() ? r["direct"].get<int>() : 128;
        info->space = str(r, "space", info->name);
        info->headName = str(r, "head_name", "FABIAN");
        if (r.contains("head") && r["head"].is_object() && r["head"].contains("file") && r["head"]["file"].is_string()) {
            const auto& hd = r["head"];
            info->headFile = hd["file"].get<std::string>();
            info->headFrom = num(hd, "from_deg", -50);
            info->headStep = num(hd, "step_deg", 2);
            info->headGainDb = num(hd, "gain_db", info->gainDb);
        }
        if (r.contains("flat") && r["flat"].is_object() && r["flat"].contains("file") && r["flat"]["file"].is_string()) {
            const auto& fl = r["flat"];
            info->flatFile = fl["file"].get<std::string>();
            info->flatGainDb = num(fl, "gain_db", info->gainDb);
            info->flatHeadFile = str(fl, "head_file", "");
            info->flatHeadGainDb = num(fl, "head_gain_db", info->gainDb);
        }
        if (r.contains("speaker") && r["speaker"].is_object()) {
            info->hpHz = num(r["speaker"], "hp_hz", 0);
            info->hpOrder = int(num(r["speaker"], "hp_order", 0));
        }
        out.push_back(info);
    }
    return out;
}

void RoomResponse::highpass(double hz, int order) {
    if (hz <= 0 || order < 1) return;
    for (size_t i = 0; i < dd.size(); ++i) {
        const double x = std::pow(MonitorDesign::pointHz(int(i)) / hz, double(2 * order));
        const double g = x / (1 + x);
        dd[i] *= g;
        dt[i] *= g;
        tt[i] *= g;
    }
}

std::shared_ptr<RoomIR> RoomIR::fromWav(const std::string& name, const std::vector<uint8_t>& wav, double gainDb,
                                        int direct) {
    const auto w = parseWav(wav);
    if (!w || w->channels != 4 || w->rate != 48000 || w->frames <= 0) return nullptr;
    const int n = std::min(w->frames, maxFrames);
    const float g = float(std::pow(10.0, gainDb / 20));
    std::vector<float> p(size_t(4 * n), 0.f);
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < n; ++i) p[size_t(c * n + i)] = w->samples[size_t(i * 4 + c)] * g;
    return std::make_shared<RoomIR>(name, std::move(p), n, std::max(0, direct));
}

std::shared_ptr<RoomIR> RoomIR::mixed(const RoomIR* f, double raw) const {
    auto self = std::make_shared<RoomIR>(name, h, frames, direct);
    if (!f || f->frames > frames || f->frames <= 0 || raw >= 0.9995) return self;
    const float t = float(std::max(0.0, raw));
    const int n = f->frames;
    const int fade = std::min(n, 480);
    std::vector<float> p = h;
    for (int path = 0; path < 4; ++path) {
        for (int i = 0; i < n; ++i) {
            float w = t;
            if (i >= n - fade) w = t + (1 - t) * float(i - (n - fade) + 1) / float(fade);
            const size_t k = size_t(path * frames + i);
            const float fl = f->h[size_t(path * n + i)];
            p[k] = fl + (h[k] - fl) * w;
        }
    }
    return std::make_shared<RoomIR>(name, std::move(p), frames, direct);
}

std::optional<RoomIR::Wav> RoomIR::parseWav(const std::vector<uint8_t>& b) {
    if (b.size() < 12 || b[0] != 0x52 || b[1] != 0x49 || b[2] != 0x46 || b[3] != 0x46) return std::nullopt;
    auto u16 = [&](size_t i) { return int(b[i]) | int(b[i + 1]) << 8; };
    auto u32 = [&](size_t i) { return int64_t(u16(i)) | int64_t(u16(i + 2)) << 16; };
    size_t at = 12;
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    while (at + 8 <= b.size()) {
        const std::string id(reinterpret_cast<const char*>(&b[at]), 4);
        const size_t size = size_t(u32(at + 4));
        const size_t body = at + 8;
        if (id == "fmt " && body + 16 <= b.size()) {
            fmt = u16(body);
            ch = u16(body + 2);
            rate = int(u32(body + 4));
            bits = u16(body + 14);
        } else if (id == "data") {
            if (ch <= 0 || bits % 8 != 0 || bits < 16) return std::nullopt;
            const int bytes = bits / 8;
            const size_t end = std::min(b.size(), body + size);
            const int n = int((end - body) / size_t(bytes * ch));
            Wav w{ch, rate, n, std::vector<float>(size_t(n * ch), 0.f)};
            size_t i = body;
            for (int k = 0; k < n * ch; ++k) {
                float v = 0;
                if (fmt == 3 && bytes == 4) {
                    const uint32_t bitsv = uint32_t(u32(i));
                    std::memcpy(&v, &bitsv, 4);
                } else if (bytes == 2) {
                    v = float(int16_t(uint16_t(u16(i)))) / 32768;
                } else if (bytes == 3) {
                    const int x = int(b[i]) | int(b[i + 1]) << 8 | int(b[i + 2]) << 16;
                    v = float(x >= 0x800000 ? x - 0x1000000 : x) / 8388608;
                } else if (bytes == 4) {
                    v = float(int32_t(uint32_t(u32(i)))) / 2147483648.0f;
                } else {
                    return std::nullopt;
                }
                w.samples[size_t(k)] = v;
                i += size_t(bytes);
            }
            return w;
        }
        at = body + size + (size & 1);
    }
    return std::nullopt;
}

RoomResponse RoomIR::response(double sr) const {
    const int n = frames;
    const int pts = MonitorDesign::points;
    RoomResponse out(pts);
    int size = 256;
    while (size < n) size *= 2;
    PvFFT fft(size);
    const int bins = size / 2 + 1;
    std::vector<float> x(size_t(size), 0.f), re(size_t(4 * bins)), im(size_t(4 * bins));
    x[0] = 1;
    fft.forward(x.data(), re.data(), im.data());
    const double unit = double(re[1] * re[1] + im[1] * im[1]);
    for (int part = 0; part < 2; ++part) {
        for (int ear = 0; ear < 2; ++ear) {
            std::fill(x.begin(), x.end(), 0.f);
            for (int i = 0; i < n; ++i)
                if ((i < direct) == (part == 0))
                    x[size_t(i)] = h[size_t(ear * n + i)] + h[size_t((2 + ear) * n + i)];
            const int at = (part * 2 + ear) * bins;
            fft.forward(x.data(), re.data() + at, im.data() + at);
        }
    }
    const double hzPerBin = sr / double(size);
    for (int k = 0; k < pts; ++k) {
        const double lo = 20 * std::pow(1000.0, double(k) / double(pts)) / hzPerBin;
        const double hi = 20 * std::pow(1000.0, double(k + 1) / double(pts)) / hzPerBin;
        int j0 = int(std::ceil(lo));
        int j1 = int(std::ceil(hi));
        if (j1 <= j0) {
            j0 = std::min(bins - 1, int(std::round(MonitorDesign::pointHz(k) / hzPerBin)));
            j1 = j0 + 1;
        }
        j1 = std::min(j1, bins);
        double dd = 0, dt = 0, tt = 0;
        for (int j = j0; j < j1; ++j) {
            for (int ear = 0; ear < 2; ++ear) {
                const size_t a = size_t(ear * bins + j), bb = size_t((2 + ear) * bins + j);
                const double dr = re[a], di = im[a], tr = re[bb], ti = im[bb];
                dd += dr * dr + di * di;
                dt += dr * tr + di * ti;
                tt += tr * tr + ti * ti;
            }
        }
        const double m = 1 / (2 * double(j1 - j0) * unit);
        out.dd[size_t(k)] = dd * m;
        out.dt[size_t(k)] = dt * m;
        out.tt[size_t(k)] = tt * m;
    }
    return out;
}

}  // namespace jambo
