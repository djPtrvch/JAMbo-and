// Библиотека (порт AppModelLibrary.swift, ветка swift JAMbo): браузер на месте центра из меню звука (LOAD). Тап по
// сэмплу или пресету — звук дорожки (в её папке остаётся только он), удержание — выбор нескольких (LOAD — все в папку
// дорожки), для зажатых шагов — их звук. IMPORT — аудио и .jambopreset в открытую папку (файлы читает экран). Дерево —
// Library.json, сэмплы — в SampleStore. Не перенесено: SAVE (пресет, сэмпл), EXPORT, .zip, LOAD TABLE, REF, SOURCE,
// перетаскивание папок, нарезка и варпинг из библиотеки (marks).
#include "App.h"
#include <filesystem>
#include "model/AnalogMod.h"
#include "model/ProjectStore.h"
#include "model/SynthEngines.h"
#include "sampler/NoteLocks.h"
#include "sampler/Pitch.h"

namespace jambo {

void App::openLibraryStorage(const std::string& base) {
    samples_ = std::make_unique<SampleStore>(base + "/Samples");
    libStore_ = std::make_unique<LibraryStore>(base + "/Library.json");
}

void App::ensureLibrary() {
    if (libraryLoaded_ || !libStore_) return;
    library_ = libStore_->load();
    libraryLoaded_ = true;
}

void App::libEdit(const std::function<void(Library&)>& change) {
    ensureLibrary();
    change(library_);
    if (libStore_) libStore_->save(library_);
    bump();
}

std::vector<std::string> App::projectSampleKeys() const {
    std::set<std::string> keys;
    auto add = [&](const std::string& k) {
        if (!k.empty() && SynthKey::engine(k) < 0) keys.insert(k);
    };
    for (const auto& [k, _] : project_.files) add(k);
    for (const auto& t : project_.tracks) {
        add(t.sample);
        add(t.grain);
        for (const auto& it : t.folder.items)
            if (it.sample) add(*it.sample);
        if (t.home) add(t.home->sample);
        for (const auto& c : t.clips)
            if (c && c->src) add(c->src->sample);
    }
    return {keys.begin(), keys.end()};
}

void App::ensureSamples() {
    if (!samples_) return;
    for (const auto& k : projectSampleKeys())
        if (!samples_->data(k))
            if (auto d = SampleStore::read(k, samples_->dir())) samples_->add(k, d);
}

SampleRef App::sampleData(const std::string& key) const { return samples_ ? samples_->data(key) : nullptr; }

// MARK: — окно

void App::openBrowser(const std::vector<int>& steps) {
    ensureLibrary();
    browser_ = Browser{steps};
    libPicked_.reset();
    libNote_.clear();
    if (!library_.isFolder(libFolder_)) libFolder_ = Library::root;
    patternView_ = false;
    eqView_ = false;
    soundMenu_.reset();
    bump();
}

void App::closeBrowser() {
    browser_.reset();
    libPicked_.reset();
    stopLibraryPreview();
    bump();
}

std::vector<LibNode> App::libItems() const {
    std::vector<LibNode> out;
    for (auto& n : library_.children(libFolder_))
        if (n.kind != LibKind::table) out.push_back(n);
    return out;
}

std::string App::libPath() const {
    std::string s = "LIBRARY";
    for (const auto& n : library_.path(libFolder_)) s += "  /  " + n.name;
    return s;
}

void App::libEnter(const std::string& id) {
    if (libPicked_) return;
    libFolder_ = id;
    bump();
}

void App::libUp() {
    if (const LibNode* n = library_.node(libFolder_)) libFolder_ = n->parent;
    libPicked_.reset();
    bump();
}

void App::libNewFolder(const std::string& name) {
    const std::string into = libFolder_;
    libEdit([&](Library& l) { l.createFolder(into, name); });
}

void App::libRename(const std::string& id, const std::string& name) {
    libEdit([&](Library& l) { l.rename(id, name); });
    const LibNode* n = library_.node(id);
    if (!n || n->kind != LibKind::sample || !n->key || !project_.files.count(*n->key)) return;
    const std::string key = *n->key, shown = n->name;
    mutatePlay([&](Project& p) { p.files[key] = shown; }, false);
}

void App::libDelete(const std::string& id) {
    libEdit([&](Library& l) { l.remove(id); });
    if (libPicked_) libPicked_->erase(id);
}

// MARK: — загрузка

bool App::libIsCurrent(const LibNode& n) const {
    if (channel_ != Channel::track || libPicked_ || !browser_) return false;
    const Track& t = track();
    if (n.kind == LibKind::sample) return n.key && t.sample == *n.key;
    if (n.kind == LibKind::preset) return n.preset && t.sample == n.preset->sound && t.presetName() == n.name;
    return false;
}

std::string App::trackLabel(int i) const {
    return "Track " + (i < int(project_.tracks.size()) ? project_.tracks[size_t(i)].name : std::to_string(i + 1));
}

static std::string stepsLabel(const std::vector<int>& steps) {
    std::string s = "Step ";
    for (size_t k = 0; k < steps.size(); k++) s += (k ? ", " : "") + std::to_string(steps[k] + 1);
    return s;
}

bool App::libUseSamples(const std::vector<std::pair<std::string, std::string>>& list) {
    if (!samples_) return false;
    std::map<std::string, std::optional<int>> found;
    for (const auto& [k, _] : list)
        if (!samples_->data(k))
            if (auto d = SampleStore::read(k, samples_->dir())) {
                samples_->add(k, d);
                found[k] = PitchDetect::root(*d);
            }
    bool ok = true;
    std::vector<std::pair<std::string, std::string>> present;
    for (const auto& x : list) {
        if (samples_->data(x.first)) present.push_back(x);
        else ok = false;
    }
    if (!ok) libNote_ = "This sample is no longer stored on this device";
    if (!present.empty())
        mutatePlay([&](Project& p) {
            for (const auto& [k, name] : present) p.files[k] = name;
            for (const auto& [k, r] : found)
                if (r && !p.roots.count(k)) p.roots[k] = *r;
        }, true);
    return ok;
}

void App::libTap(const std::string& id) {
    const LibNode* found = library_.node(id);
    if (!found || !browser_) return;
    const LibNode n = *found;
    if (n.kind == LibKind::folder) {
        libEnter(n.id);
        return;
    }
    if (channel_ != Channel::track) return;
    if (libPicked_) {
        if (libPicked_->count(n.id)) libPicked_->erase(n.id);
        else libPicked_->insert(n.id);
        bump();
        return;
    }
    const auto steps = browser_->steps;
    const int i = selected_;
    if (n.kind == LibKind::sample && n.key) {
        const std::string key = *n.key;
        if (!libUseSamples({{key, n.name}})) {
            bump();
            return;
        }
        edit([&](Project& p) {
            Track& t = p.tracks[size_t(i)];
            if (steps.empty()) {
                t.setFolder({FolderItem::ofSample(key)});
                t.clearSampleLocks();
            } else {
                t.addSamplesQuiet({key});
                t.setStepSound(steps, key);
            }
        });
        libNote_ = (steps.empty() ? trackLabel(i) : stepsLabel(steps)) + ": " + n.name;
    } else if (n.kind == LibKind::preset && n.preset && steps.empty()) {
        const SynthPreset p = *n.preset;
        edit([&](Project& pr) {
            Track& t = pr.tracks[size_t(i)];
            t.setFolder({FolderItem::ofPreset(p)});
            t.clearSampleLocks();
        });
        libNote_ = trackLabel(i) + ": " + n.name;
    }
    bump();
}

void App::libHold(const std::string& id) {
    if (libPicked_ || !browser_ || !browser_->steps.empty()) return;
    const LibNode* n = library_.node(id);
    if (n && (n->kind == LibKind::sample || n->kind == LibKind::preset)) {
        libPicked_ = std::set<std::string>{id};
        bump();
    }
}

void App::libPickAll() {
    if (!libPicked_) return;
    libPicked_->clear();
    for (const auto& n : libItems())
        if (n.kind == LibKind::sample || n.kind == LibKind::preset) libPicked_->insert(n.id);
    bump();
}

void App::libCancelPick() {
    libPicked_.reset();
    bump();
}

void App::libLoadPicked() {
    if (!libPicked_ || channel_ != Channel::track) return;
    const auto picked = *libPicked_;
    std::vector<LibNode> list;
    for (const auto& n : library_.children(libFolder_))
        if (n.kind != LibKind::folder && picked.count(n.id)) list.push_back(n);
    std::vector<std::pair<std::string, std::string>> smp;
    for (const auto& n : list)
        if (n.kind == LibKind::sample && n.key) smp.push_back({*n.key, n.name});
    libUseSamples(smp);
    std::vector<FolderItem> items;
    for (const auto& n : list) {
        if (n.kind == LibKind::sample && n.key && sampleData(*n.key)) items.push_back(FolderItem::ofSample(*n.key));
        if (n.kind == LibKind::preset && n.preset) items.push_back(FolderItem::ofPreset(*n.preset));
    }
    libPicked_.reset();
    if (items.empty()) {
        bump();
        return;
    }
    const int i = selected_;
    edit([&](Project& p) {
        p.tracks[size_t(i)].setFolder(items);
        p.tracks[size_t(i)].clearSampleLocks();
    });
    closeBrowser();
}

// MARK: — IMPORT

std::string App::libImportFolder() const { return libFolder_; }

void App::libImported(const std::string& folder, const SampleStore::Loaded& l) {
    if (!samples_) return;
    samples_->add(l.key, l.data);
    libEdit([&](Library& lib) {
        lib.addSample(library_.isFolder(folder) ? folder : std::string(Library::root), l.key, l.name);
    });
}

bool App::libImportPreset(const std::string& folder, const std::string& name, const std::string& data) {
    std::string base = name;
    const std::string ext = ".jambopreset";
    if (base.size() > ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0)
        base.resize(base.size() - ext.size());
    // { jambo: 'preset', preset }; без имени — имя файла.
    const Json j = Json::parse(data, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    const auto it = j.find("preset");
    if (it == j.end() || !it->is_object() || !it->contains("sound") || !(*it)["sound"].is_string() ||
        !it->contains("params") || !(*it)["params"].is_object())
        return false;
    auto p = ProjectCoding::decodeSynthPreset(it->dump());
    if (!p) return false;
    if (p->name.empty()) p->name = base;
    const std::string into = library_.isFolder(folder) ? folder : std::string(Library::root);
    libEdit([&](Library& lib) { lib.addPreset(into, *p); });
    return true;
}

void App::setLibNote(const std::string& s) {
    libNote_ = s;
    bump();
}

// MARK: — предпрослушка ▶

void App::libPreview(const std::string& id) {
    const LibNode* n = library_.node(id);
    if (!n) return;
    if (n->kind == LibKind::sample && n->key) {
        const std::string key = *n->key;
        if (!sampleData(key) && samples_)
            if (auto d = SampleStore::read(key, samples_->dir())) samples_->add(key, d);
        if (!sampleData(key)) {
            setLibNote("This sample is no longer stored on this device");
            return;
        }
        // Снимок с сэмплом вне дорожек — раньше команды.
        if (libPreviewKey_ != key) {
            libPreviewKey_ = key;
            publishAudio();
        }
        previewOn_ = true;
        audio_->push(CoreEvent::preview(SoundKey::hash(key), 0, 1));
    } else if (n->kind == LibKind::preset && n->preset && channel_ == Channel::track) {
        const SynthPreset& p = *n->preset;
        ParamMap pp = p.params;
        for (const char* aid : {AnalogMod::modId, AnalogMod::octId})
            if (auto v = p.value(aid)) pp[aid] = *v;
        audio_->push(CoreEvent::presetNote(selected_, Params::sampleRoot, SoundKey::hash(p.sound), NoteLocks(pp)));
    }
}

void App::stopLibraryPreview() {
    if (!libPreviewKey_) return;
    if (previewOn_) audio_->push(CoreEvent::make(CoreEvent::Kind::previewStop));
    previewOn_ = false;
    libPreviewKey_.reset();
    publishAudio();
}

}  // namespace jambo
