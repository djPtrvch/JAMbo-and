// Сэмпл в памяти и ключ звука (порт Core/Sampler/SampleData.swift, ветка swift JAMbo): 48 кГц, float32, моно или
// стерео (у моно right — тот же буфер, что left). Создаётся вне колбэка; колбэк читает только указатели.
// Не перенесено: loopBpm (TempoDetect), onsets (Onsets) — с таймстретчем; SampleEdit — с редактором сэмпла.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace jambo {

class SampleData {
public:
    SampleData(const std::vector<float>& left, const std::vector<float>* right);
    /// Пустой сэмпл (тишина) на n кадров.
    SampleData(int n, bool stereo);
    SampleData(const SampleData&) = delete;
    SampleData& operator=(const SampleData&) = delete;

    int frames() const { return frames_; }
    int channels() const { return channels_; }
    float* left() const { return l.get(); }
    float* right() const { return r ? r.get() : l.get(); }
    double duration() const;
    /// Период источника, с (метаданные; 0 — неизвестен): GRANULAR выравнивает по нему зёрна.
    double period = 0;

private:
    int frames_;
    int channels_;
    std::unique_ptr<float[]> l, r;
};

using SampleRef = std::shared_ptr<SampleData>;

/// Ключ звука в снимках для колбэка: FNV-1a 64 от строки (0 — нет звука).
namespace SoundKey {
uint64_t hash(const std::string& s);
}

}  // namespace jambo
