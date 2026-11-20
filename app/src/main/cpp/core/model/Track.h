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

    // Папка звуков, свой звук шага, запись на дорожку (Sounds.swift): Sounds.cpp
    /// Добавить звуки в папку (уже лежащий там сэмпл не повторяется, пресет — добавляется); номер первого (−1 — пусто).
    int addItems(const std::vector<FolderItem>& items);
    /// Звук из папки на дорожку: пресет — движок, его ручки и таблица.
    void applyItem(const FolderItem& it);
    /// Загруженные файлы — в папку, первый звучит на дорожке.
    void addSamples(const std::vector<std::string>& keys);
    /// Файлы для зажатых шагов: в папку, не меняя звук дорожки.
    void addSamplesQuiet(const std::vector<std::string>& keys);
    /// Библиотека: в папке дорожки — только эти звуки, первый звучит.
    void setFolder(const std::vector<FolderItem>& items);
    /// Таблица WTABLE в папке дорожки (пресет с движком и таблицей); дорожка не WTABLE — nullopt.
    static std::optional<FolderItem> tableItem(const Track& t, const std::string& key, const std::string& name);
    /// Пресет из синта дорожки; у сэмпла — nullopt.
    std::optional<SynthPreset> preset(const std::string& name) const;
    /// Имя пресета, если звук дорожки — пресет из её папки ("" — нет).
    std::string presetName() const;
    /// Стрелки: соседний звук папки на дорожку.
    void stepFolder(int dir);
    /// Номер звука шага в папке или −1.
    int stepItemIndex(const Step& s) const;
    /// Сменился звук шага — p-lock прежнего пресета звука убираются.
    static void dropSoundLocks(Step& s);
    /// Зажат шаг + стрелка: шагу — соседний звук папки.
    void stepFolderOnSteps(const std::vector<int>& indices, int dir);
    /// Снять свои сэмплы шагов во всех клипах (свой синт шага остаётся).
    void clearSampleLocks();
    /// Свой звук шагов (nullopt — снова звук дорожки).
    void setStepSound(const std::vector<int>& indices, const std::optional<std::string>& key);
    /// Подпись между стрелками: номер (с 1) и всего; held — зажатый шаг (его звук).
    std::pair<int, int> folderPos(std::optional<int> held) const;
    /// CLEAR в редакторе: сэмпл убирается с дорожки и из её папки.
    void clearSample();
    /// Отредактированная копия или запись — на место прежнего сэмпла (и в папке).
    void replaceSample(const std::string& old, const std::string& next);
    /// REC: был сэмпл — запись на его место (и в папке); иначе — в папку и звучит.
    void addRecordedSample(const std::string& key);

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
    /// В клипе k есть триги (на играющих страницах).
    bool clipHasTrigs(int k) const;
    /// Длина прохода клипа k в шагах транспорта (с учётом скорости); пустая ячейка — такт.
    int clipSteps(int k) const;
    /// Самый длинный блок, который ставит запись и перенос клипов: 8 тактов.
    static constexpr int maxClipBlock = 8 * ArrGrid::stepsPerBar;
    /// Блок клипа при записи и переносе: по проходу клипа, не длиннее 8 тактов; skipEmpty — участок без тригов не
    /// пишется (тогда может вернуть −1). Номер первого блока.
    int placeClip(const ArrBlock& b, bool skipEmpty = true);
    /// Есть ли в участке блока b триг его клипа.
    bool clipHasTrig(const ArrBlock& b) const;
    int addBlock(int clip, int at, std::optional<int> length = std::nullopt);
    int moveBlock(int i, int start);
    int resizeBlock(int i, int start, int end);
    void deleteBlock(int i);
    /// DELETE с магнитом: блоки правее сдвигаются влево на его длину.
    void deleteBlockClosing(int i);
    int duplicateBlock(int i);
    int pasteBlock(int after, const ArrBlock& b);
    /// Блоки, которые начинаются не раньше x, сдвигаются на d (блок через x сначала делится).
    void shiftArrange(int x, int d);
    /// INSERT: блок клипа в точку at, блоки правее сдвигаются на его длину.
    int insertBlock(int clip, int at, std::optional<int> length = std::nullopt);
    int resizeInserted(int i, int length);
    /// Блоки дорожки ещё раз — после последнего.
    void duplicateBlocks();
    /// Номера клипов в блоках после правки строк (nullopt — блок удаляется).
    void remapBlocks(const std::function<std::optional<int>(int)>& map);
    /// Конец последнего блока (0 — блоков нет).
    int arrangeEnd() const;
    /// Разрезать блок, через который проходит x.
    void splitArrange(int x);
    void splitBlock(int x);
    /// Клип, который играет блок: своя копия, иначе клип сетки (nullopt — ячейка пуста).
    std::optional<Clip> blockClip(const ArrBlock& b) const;
    /// У каждого блока — своя копия клипа (ArrangeRecorder.cpp). true — что-то поменялось.
    bool ownArrPatterns();

    // Автоматизация (ArrangeAuto.swift): ArrangeAuto.cpp
    int autoLane(const std::string& id) const;
    std::vector<std::vector<ArrAutoPoint>> autoGroups(const std::string& id) const;
    std::vector<ArrAutoPoint> autoPoints(const std::string& id) const;
    std::optional<double> autoValue(const std::string& id, double x) const;
    void setAuto(const std::string& id, const std::vector<ArrAutoPoint>& points);
    /// Записанный кусок кривой: точки на участке [первая, последняя] заменяются куском (упрощённым).
    void recordAuto(const std::string& id, const std::vector<ArrAutoPoint>& take, double tol = 0.01);
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
    /// Звук по активной ячейке: у клипа со своим звуком (src) — он (свой — в home), у клипа без своего — звук дорожки.
    void settleSound();
    /// Сделать ячейку k активной; открытый клип идёт за активным, если был им (Clips.swift).
    void activate(int k);
    /// Активная ячейка — k (open не трогается): звук прежнего клипа со своим звуком — в его src, звук дорожки — из home.
    void moveSlot(int k);
    /// Громкость (dB), с которой звучит клип k: своя у клипа со своим звуком, иначе — дорожки.
    double volSlot(int k) const;
    /// Звук, которым звучит клип k.
    TrackSound soundSlot(int k) const;

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

    // Запись с REC (Recording.swift): Recording.cpp
    /// Ноты на шаг index: merge — к записанным в том же дубле; overdub — к стоящим; velocity (0.2…1) — сила нот.
    void recordNotes(int index, const std::vector<int>& notes, bool merge, bool overdub, std::optional<double> velocity);
    /// Отпустили клавишу: длина записанной ноты (не меньше 1, не дальше конца паттерна).
    void recordLength(int index, int len);
    /// OVERDUB OFF: шаг, по которому проходит запись, очищается; false — шаг и так пустой.
    bool clearPassStep(int index);
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
