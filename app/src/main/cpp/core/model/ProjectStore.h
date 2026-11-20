// Файл проекта (порт ProjectInfo, ProjectFile, ProjectFormat из Core/Model/ProjectStore.swift, ветка swift JAMbo):
// формат «jambo-swift», тот же, что у iPad; проекты на диске — ProjectStore.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "KnobDefaults.h"
#include "Project.h"

namespace jambo {

/// Запись в списке проектов (без содержимого).
struct ProjectInfo {
    std::string id;
    std::string name;
    /// Время изменения, секунды от 1970.
    double updated = 0;
    /// У Autosave — проект, из которого она сделана.
    std::optional<std::string> projectId;
    std::optional<std::string> projectName;
    /// Темп, корневая нота и гамма — для строки списка.
    double bpm = 120;
    int root = 0;
    int scale = 0;

    auto tie() const { return std::tie(id, name, updated, projectId, projectName, bpm, root, scale); }
    JAMBO_EQ(ProjectInfo)
};

/// Файл проекта: формат, версия формата, запись списка, содержимое.
struct ProjectFile {
    std::string format;
    int version = 1;
    ProjectInfo info;
    Project doc;

    /// Новый файл: темп, нота и гамма — в запись списка.
    static ProjectFile make(const ProjectInfo& info, const Project& doc);
    auto tie() const { return std::tie(format, version, info, doc); }
    JAMBO_EQ(ProjectFile)
};

namespace ProjectFormat {
extern const char* const name;  // "jambo-swift"
constexpr int version = 1;
/// JSON файла; nullopt — не записался.
std::optional<std::string> encode(const ProjectFile& file);
/// Чтение: не файл проекта — nullopt; затем repair.
std::optional<ProjectFile> decode(const std::string& data);
}  // namespace ProjectFormat

/// JSON отдельных записей (общие для проектов: свои значения ручек, пресеты синта).
namespace ProjectCoding {
std::string encode(const KnobDefaults& k);
/// Не читается — пустые KnobDefaults; прежняя ручка Filter переводится один раз.
KnobDefaults decodeKnobDefaults(const std::string& data);
std::string encode(const SynthPreset& p);
std::optional<SynthPreset> decodeSynthPreset(const std::string& data);
}  // namespace ProjectCoding

/// Проекты на диске (папка — по одному файлу <id>.json). Запись — атомарная (сбой посреди записи не портит прежний
/// файл): во временный файл, затем переименование.
class ProjectStore {
public:
    static const char* const autosaveId;  // "autosave"
    explicit ProjectStore(std::string dir);
    const std::string& dir() const { return dir_; }
    static std::string newId();
    /// Путь файла — только из букв и цифр (id приложения), иначе nullopt.
    std::optional<std::string> path(const std::string& id) const;
    /// Список: новые сверху. Нечитаемые файлы пропускаются.
    std::vector<ProjectInfo> list() const;
    std::optional<ProjectFile> load(const std::string& id) const;
    /// Сохранить (info.id — имя файла; bpm, нота, гамма — из doc).
    bool save(const Project& doc, const ProjectInfo& info) const;
    void remove(const std::string& id) const;
    /// Переименовать: меняется только имя (содержимое и время — прежние).
    bool rename(const std::string& id, const std::string& name) const;
    /// Проект с таким именем; Autosave не считается.
    std::optional<ProjectInfo> find(const std::string& name) const;

private:
    std::string dir_;
};

}  // namespace jambo
