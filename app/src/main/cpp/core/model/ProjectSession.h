// Текущий проект (порт Core/Model/ProjectSession.swift, ветка swift JAMbo): id и имя проекта, в который сохраняет
// SAVE; что записано кнопкой SAVE (пометка «unsaved») и автосохранением. Автосохранение пишет не в проект, а в
// запись Autosave (с id и именем проекта); при запуске открывается она, но SAVE по-прежнему сохраняет в тот проект.
#pragma once
#include "ProjectStore.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jambo {

struct ProjectSession {
    static const char* const autosaveName;  // "Autosave"
    /// Пауза после изменений до автосохранения, с.
    static constexpr double autosaveDelay = 1.5;
    /// Длина имени проекта (24 знака).
    static constexpr int nameLimit = 24;

    std::string id;
    std::string name;
    /// Проект в том виде, в каком его записала кнопка SAVE (nullopt — ещё не сохраняли).
    std::optional<Project> saved;
    /// Что последним записало автосохранение.
    std::optional<Project> lastAuto;

    ProjectSession() = default;
    ProjectSession(std::string id_, std::string name_, std::optional<Project> saved_ = std::nullopt,
                   std::optional<Project> lastAuto_ = std::nullopt)
        : id(std::move(id_)), name(std::move(name_)), saved(std::move(saved_)), lastAuto(std::move(lastAuto_)) {}

    bool unsaved(const Project& p) const { return !saved || !(*saved == p); }
    bool needsAutosave(const Project& p) const { return !lastAuto || !(*lastAuto == p); }
    ProjectInfo autosaveInfo(double now) const;
    ProjectInfo info(double now) const;

    /// Имя нового проекта: «Project N», первое свободное.
    static std::string nextName(const std::vector<ProjectInfo>& list);
    /// Случайное имя для первого SAVE: «Прилагательное Существительное», не занятое (иначе — с номером).
    static std::string randomName(const std::vector<ProjectInfo>& list, std::optional<uint64_t> seed = std::nullopt);
    static const std::vector<std::string> nameAdjectives, nameNouns;

    static ProjectSession fresh(const ProjectStore& store);
    /// Открыть запись: проект — как есть, Autosave — в проект, из которого она сделана.
    static ProjectSession open(const ProjectFile& f, const ProjectStore& store);
    /// При запуске: Autosave, иначе последний открытый проект, иначе новый.
    static std::pair<ProjectSession, Project> start(const ProjectStore& store, const std::optional<std::string>& last);

    /// Имя из поля ввода: без пробелов по краям, не длиннее 24 знаков.
    static std::string clean(const std::string& name);
    bool save(const Project& p, const ProjectStore& store, double now);
    /// SAVE AS: копия под новым именем (дальше работа в копии); replace — проект с тем же именем (перезапись).
    bool saveAs(const Project& p, const std::string& name, const std::optional<ProjectInfo>& replace,
                const ProjectStore& store, double now);
    void rename(const std::string& name, const ProjectStore& store);
    void rename(const std::string& other, const std::string& name, const ProjectStore& store);
    /// Удалить запись (корзина): удалили текущий — он снова не сохранён.
    void remove(const std::string& other, const ProjectStore& store);
    /// Проект с таким именем, кроме текущего и Autosave.
    std::optional<ProjectInfo> named(const std::string& name, const std::vector<ProjectInfo>& list) const;
    /// «120 · C Min».
    static std::string meta(const ProjectInfo& i);
    static std::string keyName(const ProjectInfo& i);
    static std::string importedName(const std::string& name, const std::vector<ProjectInfo>& list);
    static std::string exportFileName(const std::string& name);
};

}  // namespace jambo
