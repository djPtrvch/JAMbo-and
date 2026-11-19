// Таблицы движка WTABLE (порт WtTable из Core/Jambo/Wavetable.swift): встроенные — по спектру кадров
// (гармоники 1…1023 при кадре 2048); мипмапы по октавам — обратным FFT с обрезкой гармоник (уровень L — гармоники
// до 1024 >> L), вне аудиопотока. Свои таблицы (WAV) — позже.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace jambo {

/// Ключ таблицы дорожки (Track.table; порт enum WavetableKey): "" — BASIC, "builtin:NAME" — встроенная,
/// «wt:<ключ WAV>:<размер кадра>» — своя (разбор parseUser — со своими таблицами, позже).
namespace WavetableKey {
inline std::string builtin(const std::string& name) { return name == "BASIC" ? "" : "builtin:" + name; }
inline std::string name(const std::string& key) {
    if (key.empty()) return "BASIC";
    if (key.rfind("builtin:", 0) == 0) return key.substr(8);
    return "TABLE";
}
inline std::string user(const std::string& wav, int frameSize) { return "wt:" + wav + ":" + std::to_string(frameSize); }
}  // namespace WavetableKey

/// Таблица для колбэка — только указатели (WtTable живёт в кэше).
struct WtRef {
    int frames = 0;
    const int* sizes = nullptr;
    const float* const* data = nullptr;
};

class WtTable {
public:
    static constexpr int tableSize = 2048;
    static constexpr int builtinFrames = 64;
    static constexpr int levelCount = 11;
    static constexpr int harmonics = tableSize / 2 - 1;
    static const char* const builtins[6];

    explicit WtTable(int frames);
    /// Встроенная таблица по имени (BASIC — и для неизвестного).
    static std::unique_ptr<WtTable> builtin(const std::string& name);
    WtRef ref() const { return {frames, sizes_, ptrs_}; }

    const int frames;

    /// Спектр кадров: re/im гармоник 1…harmonics.
    struct Spectra {
        int frames;
        std::vector<std::vector<double>> re, im;
    };
    static Spectra builtinSpectra(const std::string& name, int frames = builtinFrames, int limit = harmonics);

private:
    static void fft(std::vector<double>& re, std::vector<double>& im, bool inverse);
    static std::unique_ptr<WtTable> buildMips(const Spectra& sp, bool perFrame);
    static Spectra spectraFrom(int frames, int limit, const std::function<double(double, int)>& amp,
                               const std::function<double(double, int)>& phase);

    int sizes_[levelCount];
    std::vector<std::vector<float>> data_;
    const float* ptrs_[levelCount];
};

}  // namespace jambo
