// Аудиофайлы сэмплов (отличие Android: на iPad их читает AVAudioFile). Чтение WAV (PCM 8–32 бит, float 32/64,
// EXTENSIBLE), AIFF/AIFC (целые, sowt, fl32/fl64) и CAF (lpcm); запись CAF float32 48 кГц — своя копия сэмпла, как
// Samples/<ключ>.caf у iPad. Прочие форматы (MP3, M4A, FLAC, видео) декодирует экран (MediaCodec) и передаёт PCM.
// Больше двух каналов — первые два. Частота — к 48 кГц (Resample).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jambo {

/// Отсчёты файла: по каналам (1 или 2), частота файла.
struct PcmAudio {
    std::vector<float> left;
    std::vector<float> right;  // пусто — моно
    double rate = 48000;
    int frames() const { return int(left.size()); }
};

namespace AudioFile {
/// WAV, AIFF или CAF из байтов; не распознан или пуст — nullopt.
std::optional<PcmAudio> parse(const uint8_t* data, size_t n);
/// CAF float32 (little-endian, чередование каналов) — байты файла.
std::vector<uint8_t> caf(const std::vector<float>& left, const std::vector<float>* right, double rate = 48000);
/// WAV 32-бит float (как WavFile.float32 у iPad) — для EXPORT.
std::vector<uint8_t> wavFloat32(const std::vector<float>& left, const std::vector<float>* right, int rate = 48000);
}  // namespace AudioFile

namespace Resample {
/// Отсчёты с частоты from на to (окно-синк, Кайзер); from == to — копия.
std::vector<float> run(const std::vector<float>& x, double from, double to);
}  // namespace Resample

}  // namespace jambo
