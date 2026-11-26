// Своя таблица (WavetableUser.swift): левый канал WAV без пересчёта частоты,
// кадры → FFT → мипмапы с общим пиком.
#include "Wavetable.h"
#include "files/SampleStore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace jambo {

namespace {
bool validFrameSize(int size) { return size == 256 || size == 512 || size == 1024 || size == 2048; }
}

std::optional<WavetableKey::User> WavetableKey::parseUser(const std::string& key) {
    if (key.rfind("wt:", 0) != 0) return std::nullopt;
    const auto colon = key.find(':', 3);
    if (colon == std::string::npos) return std::nullopt;
    const auto wav = key.substr(3, colon - 3);
    if (!SampleStore::validKey(wav)) return std::nullopt;
    const auto suffix = key.substr(colon + 1);
    for (int size : {256, 512, 1024, 2048})
        if (suffix == std::to_string(size)) return User{wav, size};
    return std::nullopt;
}

std::optional<std::vector<float>> WtTable::readUserWave(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 12 || size > maxUserBytes) return std::nullopt;
    std::vector<uint8_t> data(static_cast<size_t>(size));
    std::ifstream file(path, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(data.data()), std::streamsize(size)) ||
        std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4)) return std::nullopt;
    auto audio = AudioFile::parse(data.data(), data.size());
    if (!audio) return std::nullopt;
    return std::move(audio->left);
}

std::unique_ptr<WtTable> WtTable::user(const std::vector<float>& samples, int frameSize) {
    if (!validFrameSize(frameSize) || samples.empty()) return nullptr;
    const auto frames = std::clamp(samples.size() / size_t(frameSize), size_t(1), size_t(maxUserFrames));
    Spectra sp{int(frames), {}, {}};
    const int limit = std::min(harmonics, frameSize / 2 - 1);
    std::vector<double> a(size_t(frameSize), 0), b(size_t(frameSize), 0);
    for (size_t frame = 0; frame < frames; ++frame) {
        for (int j = 0; j < frameSize; ++j) {
            const size_t k = frame * size_t(frameSize) + size_t(j);
            a[size_t(j)] = k < samples.size() && std::isfinite(samples[k]) ? samples[k] : 0;
            b[size_t(j)] = 0;
        }
        fft(a, b, false);
        std::vector<double> re(harmonics + 1, 0), im(harmonics + 1, 0);
        for (int n = 1; n <= limit; ++n) {
            re[n] = a[n] / frameSize;
            im[n] = b[n] / frameSize;
        }
        sp.re.push_back(std::move(re));
        sp.im.push_back(std::move(im));
    }
    return buildMips(sp, false);
}

}  // namespace jambo
