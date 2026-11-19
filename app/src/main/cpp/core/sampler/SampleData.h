// Сэмпл в памяти и ключ звука (порт Core/Sampler/SampleData.swift, ветка swift JAMbo): 48 кГц, float32, моно или
// стерео (у моно right — тот же буфер, что left). Создаётся вне колбэка; колбэк читает только указатели.
// SampleEdit (TRIM, NORMALIZE, MONO, peaks) — с редактором сэмпла.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "TempoDetect.h"

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
    /// Темп лупа для RATE в TSTR (TempoDetect::loopBpm); считается один раз, в основном потоке.
    double loopBpm() {
        if (!loopBpm_) loopBpm_ = TempoDetect::loopBpm(*this);
        return *loopBpm_;
    }
    /// Атаки (Onsets::detect) — для таймстретча NORMAL; один раз, в основном потоке.
    const OnsetList& onsets() {
        if (!onsets_) onsets_ = std::make_unique<OnsetList>(OnsetList{Onsets::detect(*this)});
        return *onsets_;
    }

private:
    std::optional<double> loopBpm_;
    std::unique_ptr<OnsetList> onsets_;
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
