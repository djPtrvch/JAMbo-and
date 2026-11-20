// SHA-256 (FIPS 180-4): ключ сэмпла по содержимому — как SampleStore.key у iPad (первые 24 hex-знака).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace jambo {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t n);
    std::array<uint8_t, 32> finish();
    /// Первые 24 знака hex хэша байтов (ключ сэмпла).
    static std::string key(const void* data, size_t n);
    static std::string hex24(const std::array<uint8_t, 32>& d);

private:
    void block(const uint8_t* p);
    uint32_t h[8];
    uint8_t buf[64];
    size_t used = 0;
    uint64_t total = 0;
};

}  // namespace jambo
