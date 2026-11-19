// REC в редакторе сэмпла (порт Core/Engine/Capture.swift, ветка swift JAMbo): запись выхода дорожки или мастера, не
// дольше 60 с; пики записи по блокам — форма волны во время записи.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace jambo {

namespace Capture {
constexpr int maxSeconds = 60;
constexpr int64_t maxFrames = 2880000;
constexpr int peakBlock = 128;
constexpr int minSpan = 1875;
/// Время записи «м:сс».
inline std::string time(double seconds) {
    const int s = std::max(0, int(std::floor(seconds)));
    const int ss = s % 60;
    return std::to_string(s / 60) + ":" + (ss < 10 ? "0" : "") + std::to_string(ss);
}
}  // namespace Capture

struct RecPeaks {
    std::vector<float> lo[2], hi[2];
    int frames = 0;
    int count() const { return int(lo[0].size()); }
    int span() const { return std::max(count(), Capture::minSpan); }
    void update(const std::vector<float>& left, const std::vector<float>& right) {
        const int n = int(std::min(left.size(), right.size()));
        const int b = Capture::peakBlock;
        while (frames + b <= n) {
            for (int c = 0; c < 2; ++c) {
                const std::vector<float>& d = c == 0 ? left : right;
                float l = 0, h = 0;
                for (int i = frames; i < frames + b; ++i) {
                    if (d[size_t(i)] < l) l = d[size_t(i)];
                    if (d[size_t(i)] > h) h = d[size_t(i)];
                }
                lo[c].push_back(l);
                hi[c].push_back(h);
            }
            frames += b;
        }
    }
    std::pair<float, float> range(int c, int k0, int k1) const {
        float l = 0, h = 0;
        const int e = std::min(k1, count());
        for (int k = std::max(0, k0); k < e; ++k) {
            if (lo[c][size_t(k)] < l) l = lo[c][size_t(k)];
            if (hi[c][size_t(k)] > h) h = hi[c][size_t(k)];
        }
        return {l, h};
    }
};

}  // namespace jambo
