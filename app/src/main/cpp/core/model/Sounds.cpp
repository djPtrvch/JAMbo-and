// Звук дорожки: папка звуков (стрелки ◀ ▶), свой звук шага, запись на дорожку (порт части Core/Model/Sounds.swift,
// ветка swift JAMbo; маркеры нарезки — Slices.cpp, Track.cpp).
#include "AnalogMod.h"
#include "SynthEngines.h"
#include "Track.h"
#include "../synth/Jambo.h"
#include <algorithm>

namespace jambo {

namespace {
int findSample(const Folder& f, const std::string& key) {
    for (size_t k = 0; k < f.items.size(); ++k)
        if (f.items[k].sample && *f.items[k].sample == key) return int(k);
    return -1;
}
double paramOr(const ParamMap& pm, const std::string& id) {
    const auto it = pm.find(id);
    if (it != pm.end()) return it->second;
    const ParamDef* d = Params::byId(id);
    return d ? d->def : 0;
}
}  // namespace

int Track::addItems(const std::vector<FolderItem>& items) {
    int first = -1;
    for (const auto& it : items) {
        int k = it.sample ? findSample(folder, *it.sample) : -1;
        if (k < 0) {
            folder.items.push_back(it);
            k = int(folder.items.size()) - 1;
        }
        if (first < 0) first = k;
    }
    if (first >= 0) folder.index = first;
    return first;
}

void Track::applyItem(const FolderItem& it) {
    if (!it.preset) {
        // Прежняя папка (строки): ключ звука как есть.
        sample = it.sample.value_or("");
        return;
    }
    const SynthPreset& p = *it.preset;
    sample = p.sound;
    for (const auto& id : SynthPreset::paramIds())
        if (auto v = p.value(id)) params[id] = *v;
    if (p.table) table = *p.table;
    if (p.grain) grain = *p.grain;
    // Пресет с модуляцией: ручки LFO/ENV и назначения на ручки звука — его (назначения на FX — у проекта).
    if (p.mods)
        for (const auto& [id, v] : *p.mods) params[id] = v;
    if (p.assigns) assigns = *p.assigns;
}

void Track::addSamples(const std::vector<std::string>& keys) {
    std::vector<FolderItem> items;
    for (const auto& k : keys) items.push_back(FolderItem::ofSample(k));
    const int at = addItems(items);
    if (at >= 0) applyItem(folder.items[size_t(at)]);
}

void Track::addSamplesQuiet(const std::vector<std::string>& keys) {
    const int index = folder.index;
    std::vector<FolderItem> items;
    for (const auto& k : keys) items.push_back(FolderItem::ofSample(k));
    addItems(items);
    if (index >= 0) folder.index = index;
}

void Track::setFolder(const std::vector<FolderItem>& items) {
    if (items.empty()) return;
    folder = Folder{items, 0};
    applyItem(items[0]);
}

std::optional<FolderItem> Track::tableItem(const Track& t, const std::string& key, const std::string& name) {
    if (SynthKey::engine(t.sample) != 19) return std::nullopt;
    SynthPreset p;
    p.name = name;
    p.sound = t.sample;
    p.table = key;
    return FolderItem::ofPreset(p);
}

std::optional<SynthPreset> Track::preset(const std::string& name) const {
    const int e = SynthKey::engine(sample);
    if (e < 0) return std::nullopt;
    SynthPreset p;
    p.name = name;
    p.sound = sample;
    for (const auto& id : SynthPreset::paramIds()) {
        if (e != J::analog && (id == AnalogMod::modId || id == AnalogMod::octId)) continue;
        p.params[id] = paramOr(params, id);
    }
    // WTABLE (движок 19) — с таблицей дорожки, GRANULAR — с сэмплом.
    if (e == 19) p.table = table;
    if (e == J::granular) p.grain = grain;
    // Модуляция: ручки LFO/ENV и назначения на ручки звука.
    ParamMap m;
    for (const auto& id : Params::modIds()) m[id] = paramOr(params, id);
    for (const auto& id : Params::lfoSyncIds) m[id] = paramOr(params, id);
    p.mods = m;
    p.assigns = assigns;
    return p;
}

std::string Track::presetName() const {
    if (folder.index < 0 || folder.index >= int(folder.items.size())) return "";
    const auto& it = folder.items[size_t(folder.index)];
    if (!it.preset || it.preset->sound != sample) return "";
    return it.preset->name;
}

void Track::stepFolder(int dir) {
    const int n = int(folder.items.size());
    if (n == 0) return;
    if (folder.index < 0) folder.index = dir > 0 ? 0 : n - 1;
    else folder.index = ((folder.index + dir) % n + n) % n;
    applyItem(folder.items[size_t(folder.index)]);
}

int Track::stepItemIndex(const Step& s) const {
    if (!s.sound) return -1;
    for (size_t k = 0; k < folder.items.size(); ++k)
        if (folder.items[k].key() == *s.sound) return int(k);
    return -1;
}

void Track::dropSoundLocks(Step& s) {
    if (s.soundLocks)
        for (const auto& k : *s.soundLocks) s.locks.erase(k);
    s.soundLocks.reset();
}

void Track::stepFolderOnSteps(const std::vector<int>& indices, int dir) {
    const int n = int(folder.items.size());
    if (n == 0) return;
    for (int i : indices) {
        const Step* s = step(i);
        if (!s) continue;
        const int cur = stepItemIndex(*s);
        const int base = cur >= 0 ? cur : folder.index;
        const int k = base < 0 ? (dir > 0 ? 0 : n - 1) : ((base + dir) % n + n) % n;
        const FolderItem it = folder.items[size_t(k)];
        withStep(i, [&](Step& st) {
            dropSoundLocks(st);
            st.sound = it.key();
            // У пресета — ещё и p-lock его ручек (не ручные: шаг из-за них не жёлтый).
            if (it.preset) {
                std::vector<std::string> ids;
                for (const auto& id : SynthPreset::paramIds())
                    if (auto v = it.preset->value(id)) {
                        ids.push_back(id);
                        st.locks[id] = *v;
                    }
                st.soundLocks = ids;
            }
        });
    }
}

void Track::clearSampleLocks() {
    for (auto& oc : clips) {
        if (!oc) continue;
        for (auto& page : oc->pages)
            for (auto& st : page) {
                if (!st.sound || st.sound->find(':') != std::string::npos) continue;
                dropSoundLocks(st);
                st.sound.reset();
            }
    }
}

void Track::setStepSound(const std::vector<int>& indices, const std::optional<std::string>& key) {
    for (int i : indices) {
        if (!step(i)) continue;
        withStep(i, [&](Step& st) {
            dropSoundLocks(st);
            st.sound = key;
        });
    }
}

std::pair<int, int> Track::folderPos(std::optional<int> held) const {
    if (held)
        if (const Step* s = step(*held); s && s->sound) return {stepItemIndex(*s) + 1, int(folder.items.size())};
    return {folder.index + 1, int(folder.items.size())};
}

void Track::clearSample() {
    const int k = findSample(folder, sample);
    if (k >= 0) {
        folder.items.erase(folder.items.begin() + k);
        folder.index = -1;
    }
    sample = "";
}

void Track::replaceSample(const std::string& old, const std::string& next) {
    const int k = findSample(folder, old);
    if (k >= 0) {
        folder.items[size_t(k)] = FolderItem::ofSample(next);
        folder.index = k;
    } else {
        addItems({FolderItem::ofSample(next)});
    }
    sample = next;
}

void Track::addRecordedSample(const std::string& key) {
    const std::string old = sample;
    if (!old.empty() && SynthKey::engine(old) < 0) {
        const int k = findSample(folder, old);
        if (k >= 0) {
            folder.items[size_t(k)] = FolderItem::ofSample(key);
            folder.index = k;
            sample = key;
            return;
        }
    }
    addSamples({key});
}

}  // namespace jambo
