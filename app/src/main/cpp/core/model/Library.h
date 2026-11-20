// Библиотека сэмплов, пресетов синта и таблиц WTABLE (порт Core/Model/Library.swift, ветка swift JAMbo): дерево папок,
// общее для всех проектов. Дерево — filesDir/Library.json (LibraryStore, формат как у iPad), байты сэмплов — в хранилище
// сэмплов (SampleStore, по ключу). Системная папка RECORDINGS не удаляется и не переименовывается. Удаление узла
// сэмплы с диска не удаляет. Нарезка и варпинг (marks) пока хранятся как есть (JSON) — с редактором сэмпла.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Json.h"
#include "SynthPreset.h"

namespace jambo {

enum class LibKind { folder, sample, preset, table };

struct LibNode {
    std::string id;
    /// Родительская папка (Library::root — корень).
    std::string parent;
    LibKind kind = LibKind::folder;
    std::string name;
    /// Сэмпл — ключ в хранилище сэмплов; таблица — ключ таблицы («wt:<ключ>:<кадр>»).
    std::optional<std::string> key;
    std::optional<SynthPreset> preset;
    std::optional<bool> system;
    /// Время изменения, с от 1970.
    double updated = 0;
    /// Место папки в своей папке; nullopt — после расставленных, по имени.
    std::optional<double> order;

    bool isSystem() const { return system.value_or(false); }
};

struct Library {
    static const char* const root;        // "root"
    static const char* const recordings;  // "recordings"
    static constexpr int nameLimit = 40;

    std::vector<LibNode> nodes;
    /// Нарезка и варпинг сэмплов по ключу (SampleMarks) — JSON как есть.
    std::map<std::string, Json> marks;

    Library() { ensureRecordings(); }
    void ensureRecordings();

    static std::string newId();
    const LibNode* node(const std::string& id) const;
    bool isFolder(const std::string& id) const;
    /// Содержимое папки: системные, папки (по порядку, затем по имени), остальное — по имени (числа — по значению).
    std::vector<LibNode> children(const std::string& parent) const;
    std::vector<LibNode> movableFolders(const std::string& parent) const;
    void moveFolder(const std::string& parent, int from, int to);
    /// Путь к папке: от корня до неё (корень не входит).
    std::vector<LibNode> path(const std::string& id) const;
    /// Имя из поля ввода: без пробелов по краям, не длиннее 40; пустое — Untitled.
    static std::string clean(const std::string& name);
    /// Имя для сравнения: без расширения аудиофайла, без учёта регистра.
    static std::string bare(const std::string& name);
    std::string uniqueName(const std::string& parent, const std::string& name, const std::string* except = nullptr) const;
    const LibNode* sameName(const std::string& parent, const std::string& name) const;
    int countInside(const std::string& id) const;

    const LibNode& createFolder(const std::string& parent, const std::string& name);
    const LibNode& addSample(const std::string& parent, const std::string& key, const std::string& name);
    const LibNode& addTable(const std::string& parent, const std::string& key, const std::string& name);
    const LibNode& addPreset(const std::string& parent, const SynthPreset& preset);
    void overwrite(const std::string& id, const FolderItem& item);
    void rename(const std::string& id, const std::string& name);
    void remove(const std::string& id);

    /// Папка записей проекта и дня в RECORDINGS: «проект дд.мм.гггг» (без имени — Project).
    static std::string recordingFolderName(const std::string& project, int day, int month, int year);
    /// Имя записи: «дорожка чч-мм-сс».
    static std::string recordingName(const std::string& base, int h, int m, int s);
    /// Папка записей с этим именем в RECORDINGS (создаётся при первой записи) — id.
    std::string recordingsFolder(const std::string& name);
    /// Сравнение имён как localizedStandardCompare: без учёта регистра, числа — по значению.
    static bool nameLess(const std::string& a, const std::string& b);
};

/// Библиотека на диске: один JSON, запись атомарная.
struct LibraryStore {
    std::string path;
    explicit LibraryStore(std::string p) : path(std::move(p)) {}
    /// Нет файла или он не читается — пустая библиотека (с RECORDINGS).
    Library load() const;
    bool save(const Library& lib) const;
};

namespace LibraryCoding {
std::string encode(const Library& lib);
std::optional<Library> decode(const std::string& data);
}  // namespace LibraryCoding

}  // namespace jambo
