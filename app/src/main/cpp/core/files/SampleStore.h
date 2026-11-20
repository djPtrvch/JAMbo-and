// Сэмплы приложения (порт Audio/SampleStore.swift, ветка swift JAMbo): в памяти (SampleData по ключу) и на диске —
// filesDir/Samples/<ключ>.caf (float32, 48 кГц; проекты ссылаются на ключ). Ключ — по содержимому исходного файла
// (SHA-256, 24 hex-знака), сэмпл хранится один раз. decode и read — с любого потока (без состояния), add и data — в
// основном.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "AudioFile.h"
#include "sampler/SampleData.h"

namespace jambo {

class SampleStore {
public:
    explicit SampleStore(std::string dir);
    const std::string& dir() const { return dir_; }

    /// Загруженный файл: ключ, имя, данные, высота (только для индикации).
    struct Loaded {
        std::string key;
        std::string name;
        SampleRef data;
        std::optional<int> root;
    };

    SampleRef data(const std::string& key) const;
    void add(const std::string& key, SampleRef d) { samples_[key] = std::move(d); }
    const std::map<std::string, SampleRef>& samples() const { return samples_; }

    /// Байты файла name: уже есть копия — она; WAV, AIFF, CAF — читаются здесь; иначе nullopt (needsDecode — true:
    /// формат не распознан, экран декодирует его сам и зовёт fromPcm с тем же ключом).
    static std::optional<Loaded> decode(const uint8_t* bytes, size_t n, const std::string& name, const std::string& dir,
                                        bool* needsDecode = nullptr);
    /// PCM, декодированный экраном (чередование каналов): к 48 кГц, копия на диск.
    static std::optional<Loaded> fromPcm(const std::string& key, const std::string& name, const float* pcm, int frames,
                                         int channels, double rate, const std::string& dir);
    /// Сэмпл с диска по ключу; нет файла — nullptr.
    static SampleRef read(const std::string& key, const std::string& dir);
    /// Своя копия на диск.
    static bool write(const SampleData& d, const std::string& path);
    static std::string path(const std::string& key, const std::string& dir);
    /// Ключ — только буквы и цифры (имя файла).
    static bool validKey(const std::string& key);
    /// EXPORT: WAV 32-бит float 48 кГц; нет — пусто.
    static std::vector<uint8_t> wav(const SampleData& d);

private:
    std::string dir_;
    std::map<std::string, SampleRef> samples_;
};

}  // namespace jambo
