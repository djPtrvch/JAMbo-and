// Дорожка (порт Track и TrackMore, Core/Model/Project.swift, ветка swift JAMbo). Поля TrackMore («коробка» Swift —
// для объёма сборки) здесь — обычные поля дорожки.
#pragma once
#include <functional>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>
#include "Arp.h"
#include "Arrange.h"
#include "Clip.h"
#include "Roll.h"
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

    // Звуки (Sounds.swift), ударные (DrumRhythm.swift): Track.cpp
    std::vector<SliceMark> sliceMarks(const std::string& key) const;
    /// Звук дорожки — сэмпл с маркерами нарезки.
    bool hasCuts() const;
    /// Роль ударного движка дорожки; −1 — не ударный.
    int drumRole() const;

    // Аранжировщик (ArrangeEdits.swift): ArrangeEdits.cpp
    void clearArrange(int from, int to);
    /// Поставить блок: перекрытая часть других заменяется. Номер блока после сортировки.
    int placeBlock(const ArrBlock& b);
    /// После чтения: клипы в пределах, длины ≥ 1, по порядку, без перекрытий; цвет в палитре.
    void repairBlocks();

    // Автоматизация (ArrangeAuto.swift): ArrangeAuto.cpp
    int autoLane(const std::string& id) const;
    std::vector<std::vector<ArrAutoPoint>> autoGroups(const std::string& id) const;
    std::vector<ArrAutoPoint> autoPoints(const std::string& id) const;
    std::optional<double> autoValue(const std::string& id, double x) const;
    void setAuto(const std::string& id, const std::vector<ArrAutoPoint>& points);
    void deleteAuto(const std::string& id);
    void clearAllAutos();
    /// Проект прежнего формата: точки кривых дорожки — в блоки под ними.
    void migrateAutos();
    /// LINK прежних проектов: у блоков вплотную — точки на стыке.
    void dropLegacyLinks();

    /// После чтения (ProjectRepair.swift): дорожка (или MIX, MST) проекта из n дорожек.
    void repair(int n);

    // Звук дорожки (Transition.swift): Transition.cpp
    /// Звук дорожки (поля, которые сейчас действуют).
    TrackSound sound() const;
    void setSound(const TrackSound& s);

    // Клипы (Clips.swift): Clips.cpp
    bool clipFilled(int k) const { return k >= 0 && k < int(clips.size()) && clips[size_t(k)].has_value(); }
    /// Клип с пустым паттерном (одна пустая страница) в ячейку k.
    void putEmptyClip(int k, std::optional<int> scale, std::optional<int> root);

    // Правки страниц, шагов, p-lock, MORPH (TrackEdits.swift): TrackEdits.cpp
    /// Изменение числа страниц: при полной длине длина растёт; если играли все — играют все.
    void changePages(const std::function<void(std::vector<Page>&)>& change);
    /// Страницы, на которые действует меню страницы i: весь играющий диапазон, если i в нём, иначе только i.
    PageRange pageRange(int i) const;
    void setPlayPages(int a, int b);
    /// «+»: 16 пустых шагов в конец секвенции.
    void addPage();
    /// Шаги steps — сразу за последним шагом секвенции; false — больше 8 страниц.
    bool appendSteps(const std::vector<Step>& steps);
    /// DUPLICATE: копии диапазона сразу за ним; false — страниц стало бы больше 8.
    bool duplicatePages(int i);
    std::vector<Page> copyPages(int i) const;
    /// PASTE: скопированные страницы с первой страницы диапазона (не хватает — добавляются, до 8).
    void pastePages(int i, const std::vector<Page>& clip);
    /// CLEAR: все шаги диапазона пустые.
    void clearPages(int i);
    /// DELETE страниц диапазона i (single — только страница i); false — не осталось бы ни одной.
    bool deletePages(int i, bool single = false);
    enum class Nudge { left, right, up, down };
    /// ◀ ▶ — шаги диапазона по кругу, ▲ ▼ — ноты на октаву; false — ничего не изменилось.
    bool nudgePages(int i, Nudge dir);
    /// SHIFT + ▲ ▼: ноты диапазона на одну ноту гаммы; false — ничего не изменилось.
    bool transposePages(int i, int d, int scale, int root);
    void withStep(int index, const std::function<void(Step&)>& change);
    /// Тап по шагу: триг с нотами notes или снять (снятый триг забывает p-lock и свой звук).
    void toggleStep(int index, const std::vector<int>& notes);
    void setLock(int index, const std::string& id, double value);
    void clearLock(int index, const std::string& id);
    /// SHIFT + двойной тап по ручке: снять все p-lock дорожки (свой звук шага остаётся).
    void clearAllLocks();
    /// Шаг держит длинная нота с предыдущих шагов.
    bool isTail(int index) const;
    /// Ручной p-lock или velocity ноты: шаг жёлтый.
    static bool hasManualLocks(const Step& s);
    /// Параметры 4-го столбца сетки дорожки, FX и MOD: их p-lock — оранжевые.
    static const std::set<std::string>& col4Ids();
    struct LockKinds {
        bool other, col4;
    };
    static LockKinds manualLockKinds(const Step& s);
    /// P-lock громкостей (mix.vol1…16) на всех шагах: дорожка k → map(k) (−1 — снять).
    void remapMixLocks(const std::function<int(int)>& map);
    /// Перенос страницы from на место to.
    void movePage(int from, int to);
    /// MORPH: исходное значение ручки / назначения — при первом изменении; undoMorph — вернуть всё.
    void morphParam(const std::string& id);
    void morphAssign(const std::string& id);
    void undoMorph();

    // Правки нот пианоролла (RollEdits.swift): RollEdits.cpp
    void addRollNote(int index, int pitch, int len, std::optional<double> vel);
    void removeRollNote(int index, int pitch);
    void setRollNoteLen(int index, int pitch, int len);
    void setRollNoteVel(int index, int pitch, double vel);
    double rollNoteLevel(const RollNote& n) const;
    std::optional<StepCarry> rollCarry(int index, int pitch) const;
    void moveRollNote(int index, int pitch, int dest, int dp, std::optional<StepCarry>& carry);
    void moveRollNote(int index, int pitch, int dest, int dp);

    /// Значение параметра на шаге: p-lock шага, иначе ручка дорожки, иначе значение по умолчанию.
    double value(const std::string& id, const Step* step) const;

    auto tie() const {
        return std::tie(id, name, sample, params, clips, slot, open, mute, solo, stopped, vol, home, blocks, folder,
                        slices, warps, trim, table, grain, assigns, morphOrig, morphOrigAssign, morphEdited, eq, midi,
                        color, drumStyle, arp, keyOctave, autos, editBlock);
    }
    JAMBO_EQ(Track)

private:
    /// Секвенция короче своих страниц и кончается на последней странице.
    bool endsShortOnLastPage() const;
    int openIndex() const { return std::max(0, std::min(open, int(clips.size()) - 1)); }
};

}  // namespace jambo
