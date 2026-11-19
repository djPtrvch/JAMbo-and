// Порт Core/Sampler/SampleData.swift (ветка swift JAMbo).
#include "SampleData.h"
#include <algorithm>
#include <cstring>

namespace jambo {

SampleData::SampleData(int n, bool stereo) : frames_(std::max(1, n)), channels_(stereo ? 2 : 1) {
    l.reset(new float[size_t(frames_)]());
    if (stereo) r.reset(new float[size_t(frames_)]());
}

SampleData::SampleData(const std::vector<float>& left, const std::vector<float>* right)
    : SampleData(int(left.size()), right != nullptr) {
    std::memcpy(l.get(), left.data(), left.size() * sizeof(float));
    if (right) std::memcpy(r.get(), right->data(), std::min(right->size(), size_t(frames_)) * sizeof(float));
}

double SampleData::duration() const { return double(frames_) / 48000.0; }

namespace SoundKey {
uint64_t hash(const std::string& s) {
    if (s.empty()) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char b : s) {
        h ^= uint64_t(b);
        h *= 0x100000001b3ULL;
    }
    return h == 0 ? 1 : h;
}
}  // namespace SoundKey

}  // namespace jambo
