// App: проекты на диске (порт AppModelProjects.swift, ветка swift JAMbo): открытие при запуске, автосохранение в
// запись Autosave, меню PROJECTS (NEW, SAVE, SAVE AS, RENAME, удаление, открытие LOAD). MIX, IMPORT строк, EXPORT и
// IMPORT .jambo и сэмплы проекта с диска — позже. Запись — в основном потоке (в Swift — своя очередь).
#include "App.h"
#include <chrono>
#include <fstream>
#include <sstream>

namespace jambo {

namespace {
double wallNow() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}
}  // namespace

/// Последний открытый проект (UserDefaults currentProject в Swift) — файлом current в папке проектов.
std::optional<std::string> App::readCurrentId() const {
    std::ifstream f(store_->dir() + "/current");
    std::string id;
    if (!(f >> id)) return std::nullopt;
    return id;
}

void App::writeCurrentId() const {
    std::ofstream(store_->dir() + "/current", std::ios::trunc) << session_.id;
}

void App::openStorage(const std::string& dir, const Project& firstRun) {
    store_ = std::make_unique<ProjectStore>(dir);
    const bool empty = store_->list().empty();
    auto opened = ProjectSession::start(*store_, readCurrentId());
    session_ = opened.first;
    writeCurrentId();
    Project p = empty ? firstRun : opened.second;
    p.ownArrPatterns();
    loadProject(p);
    // Открытый проект сразу становится автосохранением.
    projectChanged();
    refreshProjects();
}

void App::projectChanged() {
    if (store_ && autosaveDue_ == 0) autosaveDue_ = now_ + ProjectSession::autosaveDelay;
}

void App::tickAutosave(double now) {
    if (autosaveDue_ > 0 && now >= autosaveDue_) autosave();
}

void App::autosave() {
    autosaveDue_ = 0;
    if (!store_ || !session_.needsAutosave(project_)) return;
    session_.lastAuto = project_;
    store_->save(project_, session_.autosaveInfo(wallNow()));
}

void App::flushAutosave() { autosave(); }

bool App::unsaved() const { return session_.unsaved(project_); }

void App::refreshProjects() {
    if (store_) projectList_ = store_->list();
    bump();
}

void App::writeSession(bool rewrite, const std::function<void(ProjectSession&)>& f) {
    if (!store_) return;
    f(session_);
    // Autosave пишется заново: в нём id и имя проекта.
    if (rewrite) session_.lastAuto.reset();
    writeCurrentId();
    if (rewrite) projectChanged();
    refreshProjects();
}

void App::newProject() {
    if (!store_) return;
    session_ = ProjectSession::fresh(*store_);
    writeCurrentId();
    Project p;
    knobDefaults_.apply(p);
    p.repair();
    loadProject(p);
    projectChanged();
    refreshProjects();
}

bool App::isNewProject() const {
    if (session_.saved) return false;
    for (const auto& i : projectList_)
        if (i.id == session_.id) return false;
    return true;
}

void App::saveProject() {
    const double t = wallNow();
    writeSession(true, [&](ProjectSession& s) { s.save(project_, *store_, t); });
}

void App::saveProjectAs(const std::string& name, const std::string& replaceId) {
    std::optional<ProjectInfo> replace;
    for (const auto& i : projectList_)
        if (!replaceId.empty() && i.id == replaceId) replace = i;
    const double t = wallNow();
    writeSession(true, [&](ProjectSession& s) { s.saveAs(project_, name, replace, *store_, t); });
}

std::string App::projectNamed(const std::string& name) const {
    const auto i = session_.named(name, projectList_);
    return i ? i->id : std::string();
}

void App::renameProject(const std::string& id, const std::string& name) {
    writeSession(true, [&](ProjectSession& s) { s.rename(id, name, *store_); });
}

void App::deleteProject(const std::string& id) {
    writeSession(false, [&](ProjectSession& s) { s.remove(id, *store_); });
}

void App::openProject(const std::string& id) {
    if (!store_) return;
    const auto f = store_->load(id);
    if (!f) {
        refreshProjects();
        return;
    }
    session_ = ProjectSession::open(*f, *store_);
    writeCurrentId();
    Project p = f->doc;
    p.ownArrPatterns();
    loadProject(p);
    projectChanged();
    refreshProjects();
}

}  // namespace jambo
