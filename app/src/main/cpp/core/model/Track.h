// Дорожка (порт Track и TrackMore, Core/Model/Project.swift, ветка swift JAMbo). Поля TrackMore («коробка» Swift —
// для объёма сборки) здесь — обычные поля дорожки.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Arp.h"
#include "Arrange.h"
#include "Clip.h"
#include "ModAssign.h"
#include "Scales.h"
#include "Sounds.h"
#include "TrackSound.h"
#include "Warp.h"
#include "../eq/EqModel.h"
#include "../midi/TrackMidi.h"

namespace jambo {

struct KnobDefaults;

struct Track {
    int id = 0;
    std::string name;
    std::string sample;
    ParamMap params;
    /// Клипы столбца сетки паттернов (nullopt — пустая ячейка); активная — slot, открытая — open.
    std::vector<std::optional<Clip>> clips = firstClips();
    int slot = 0;
    int open = 0;
    bool mute = false;
    bool solo = false;
    /// Остановлена в сетке паттернов.
    bool stopped = false;
    double vol = Params::volDef;
    /// Переход MIX: свой звук дорожки, пока она играет клип PREV со звуком старой дорожки.
    std::optional<TrackSound> home;
    /// Аранжировщик: блоки дорожки на таймлайне.
    std::vector<ArrBlock> blocks;

    // TrackMore
    Folder folder;
    std::map<std::string, std::vector<SliceMark>> slices;
    std::map<std::string, WarpInfo> warps;
    /// LEVEL RELATIVE: подстройка громкости, dB вниз от громкости MIX.
    double trim = 0;
    /// Таблица WTABLE ("" — BASIC).
    std::string table;
    /// Сэмпл GRANULAR ("" — нет).
    std::string grain;
    std::map<std::string, ModAssign> assigns;
    /// MORPH: значения и назначения на момент включения режима, дорожку меняли.
    ParamMap morphOrig;
    std::map<std::string, std::optional<ModAssign>> morphOrigAssign;
    bool morphEdited = false;
    Eq eq;
    TrackMidi midi;
    int color = ArrPalette::defaultColor;
    int drumStyle = 0;
    TrackArp arp;
    int keyOctave = Scales::octaveDef;
    std::vector<ArrAutoLane> autos;

    /// STEPS аранжировщика: номер блока, чья копия клипа открыта (не в файле; сравнивается, как в Swift).
    std::optional<int> editBlock;

    Track() = default;
    explicit Track(int index);
    /// Последовательность MIX или MST.
    static Track channel(const std::string& name);
    static std::vector<std::optional<Clip>> firstClips();

    /// Активный клип пуст: дорожка стоит, звук остаётся.
    bool blank() const { return !clips[size_t(slot)]; }
    /// Блок, чья копия открыта; −1 — нет.
    int editedBlock() const;
    /// Открытый клип (пустая ячейка — пустой паттерн); в STEPS аранжировщика — копия блока.
    const Clip& openClip() const;
    /// Открытый клип для правки (пустой ячейки — создаёт клип).
    Clip& editOpen();

    const std::vector<Page>& pages() const { return openClip().pages; }
    std::vector<Page>& pagesMut() { return editOpen().pages; }
    int length() const { return openClip().length; }
    void setLength(int v) { editOpen().length = v; }
    int playFrom() const { return openClip().playFrom; }
    void setPlayFrom(int v) { editOpen().playFrom = v; }
    int playTo() const { return openClip().playTo; }
    void setPlayTo(int v) { editOpen().playTo = v; }

    int pageLen(int p) const { return openClip().pageLen(p, openClip().playFrom); }
    int pageLen(int p, int from) const { return openClip().pageLen(p, from); }
    int playLen() const { return openClip().playLen(); }
    int playLen(int from, int to) const { return openClip().playLen(from, to); }
    int playIndex(int n) const { return openClip().playIndex(n); }
    const Step* step(int index) const { return openClip().step(index); }
    bool outside(int index) const { return openClip().outside(index); }

    /// Синт на дорожку: звук — «mi:<n>»; ручки 1.1–1.4 — начальные у движка при переходе с другого звука (и свои
    /// значения по умолчанию defaults); Decay больше предела движка — к пределу. (SynthEngines.swift; SynthEngines.cpp)
    void setSynth(int engine, const KnobDefaults* defaults = nullptr);

    /// Значение параметра на шаге: p-lock шага, иначе ручка дорожки, иначе значение по умолчанию.
    double value(const std::string& id, const Step* step) const;

    auto tie() const {
        return std::tie(id, name, sample, params, clips, slot, open, mute, solo, stopped, vol, home, blocks, folder,
                        slices, warps, trim, table, grain, assigns, morphOrig, morphOrigAssign, morphEdited, eq, midi,
                        color, drumStyle, arp, keyOctave, autos, editBlock);
    }
    JAMBO_EQ(Track)

private:
    int openIndex() const { return std::max(0, std::min(open, int(clips.size()) - 1)); }
};

}  // namespace jambo
