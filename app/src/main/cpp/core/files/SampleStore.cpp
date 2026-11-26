#include "SampleStore.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include "Sha256.h"
#include "sampler/Pitch.h"

namespace jambo {

namespace fs = std::filesystem;

static constexpr double rate48 = 48000;

SampleStore::SampleStore(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

SampleRef SampleStore::data(const std::string& key) const {
    const auto it = samples_.find(key);
    return it == samples_.end() ? nullptr : it->second;
}

bool SampleStore::validKey(const std::string& key) {
    if (key.empty()) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

std::string SampleStore::path(const std::string& key, const std::string& dir) {
    return (fs::path(dir) / (key + ".caf")).string();
}

static SampleRef toData(PcmAudio a) {
    if (a.rate != rate48) {
        a.left = Resample::run(a.left, a.rate, rate48);
        if (!a.right.empty()) a.right = Resample::run(a.right, a.rate, rate48);
    }
    if (a.left.empty()) return nullptr;
    return std::make_shared<SampleData>(a.left, a.right.empty() ? nullptr : &a.right);
}

SampleRef SampleStore::read(const std::string& key, const std::string& dir) {
    if (!validKey(key)) return nullptr;
    std::ifstream f(path(key, dir), std::ios::binary);
    if (!f) return nullptr;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto a = AudioFile::parse(bytes.data(), bytes.size());
    return a ? toData(std::move(*a)) : nullptr;
}

bool SampleStore::write(const SampleData& d, const std::string& p) {
    const std::vector<float> l(d.left(), d.left() + d.frames());
    std::vector<float> r;
    if (d.channels() == 2) r.assign(d.right(), d.right() + d.frames());
    const auto bytes = AudioFile::caf(l, d.channels() == 2 ? &r : nullptr, rate48);
    const std::string tmp = p + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        f.flush();
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    return !ec;
}

std::optional<SampleStore::Loaded> SampleStore::decode(const uint8_t* bytes, size_t n, const std::string& name,
                                                       const std::string& dir, bool* needsDecode) {
    if (needsDecode) *needsDecode = false;
    if (n == 0) return std::nullopt;
    const std::string key = Sha256::key(bytes, n);
    if (auto d = read(key, dir)) return Loaded{key, name, d, PitchDetect::root(*d)};
    auto a = AudioFile::parse(bytes, n);
    if (!a) {
        if (needsDecode) *needsDecode = true;
        return std::nullopt;
    }
    auto d = toData(std::move(*a));
    if (!d) return std::nullopt;
    write(*d, path(key, dir));
    return Loaded{key, name, d, PitchDetect::root(*d)};
}

std::optional<SampleStore::Loaded> SampleStore::fromPcm(const std::string& key, const std::string& name, const float* pcm,
                                                        int frames, int channels, double rate, const std::string& dir) {
    if (!validKey(key) || frames <= 0 || channels <= 0 || rate < 1000) return std::nullopt;
    PcmAudio a;
    a.rate = rate;
    a.left.resize(size_t(frames));
    if (channels >= 2) a.right.resize(size_t(frames));
    for (int i = 0; i < frames; i++) {
        a.left[size_t(i)] = pcm[size_t(i) * size_t(channels)];
        if (channels >= 2) a.right[size_t(i)] = pcm[size_t(i) * size_t(channels) + 1];
    }
    auto d = toData(std::move(a));
    if (!d) return std::nullopt;
    write(*d, path(key, dir));
    return Loaded{key, name, d, PitchDetect::root(*d)};
}

std::string SampleStore::store(const SampleData& d, const std::string& dir) {
    Sha256 h;
    h.update(d.left(), size_t(d.frames()) * 4);
    if (d.channels() == 2) h.update(d.right(), size_t(d.frames()) * 4);
    const std::string key = Sha256::hex24(h.finish());
    return write(d, path(key, dir)) ? key : std::string{};
}

std::vector<uint8_t> SampleStore::wav(const SampleData& d) {
    const std::vector<float> l(d.left(), d.left() + d.frames());
    std::vector<float> r;
    if (d.channels() == 2) r.assign(d.right(), d.right() + d.frames());
    return AudioFile::wavFloat32(l, d.channels() == 2 ? &r : nullptr);
}

}  // namespace jambo
