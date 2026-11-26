// Контейнер .jambo (ProjectPack.swift): JMB1, заголовок JSON и медиафайлы подряд.
#pragma once
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include "Project.h"

namespace jambo {

struct ProjectAssets {
    std::set<std::string> samples;
    std::set<std::string> tables;
};

ProjectAssets projectAssets(const Project& project);

namespace ProjectPack {

struct Storage {
    std::filesystem::path samples;
    std::filesystem::path tables;
};

struct Imported {
    std::string name;
    Project project;
    size_t missing = 0;
};

/// Копирует медиа потоком. Возвращает число отсутствующих файлов или nullopt при ошибке записи.
std::optional<size_t> write(const std::filesystem::path& path, const std::string& name,
                            const Project& project, const Storage& storage);

/// Сначала проверяет заголовок и границы всех вложений, затем сохраняет только медиа этого проекта.
std::optional<Imported> read(const std::filesystem::path& path, const Storage& storage);

}  // namespace ProjectPack
}  // namespace jambo
