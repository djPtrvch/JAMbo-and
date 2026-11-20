// Модель проекта (порт Project и ProjectMore, Core/Model/Project.swift, ветка swift JAMbo). Значения: копия — снимок
// для UNDO. Поля ProjectMore («коробка» Swift) здесь — обычные поля проекта. Имена полей — ключи файла проекта.
#pragma once
#include <functional>
#include <map>
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "Arrange.h"
#include "Clip.h"
#include "ModAssign.h"
#include "Params.h"
#include "Track.h"
#include "ArrangeRecorder.h"
#include "../eq/EqModel.h"
#include "../master/MasterChain.h"

namespace jambo {

/// Точка возврата MORPH: дорожка (Track::id), параметр, значение сейчас и исходное.
struct MorphPoint {
    int track;
    std::string param;
    double from, to;
    bool operator==(const MorphPoint& o) const {
        return track == o.track && param == o.param && from == o.from && to == o.to;
    }
};

/// Возврат после выключения MORPH: OFF (сразу), 1/4, 1/2 такта, такт, 2 такта (в долях).
namespace MorphReturn {
extern const std::vector<double> beats;
extern const std::vector<std::string> names;
/// Длительность возврата, с.
double seconds(int k, double bpm);
}  // namespace MorphReturn

struct Project {
    /// Версия формата (10 — нынешняя; история — ProjectRepair).
    int version = 10;
    double bpm = 120;
    int scale = 0;
    int root = 0;
    /// Общие FX: принадлежат клипам — действуют FX клипа, запущенного последним.
    ParamMap fx = Params::fxDefaults();
    std::vector<Track> tracks;

    // ProjectMore
    /// Сообщение при открытии проекта (перевод старой версии); nullopt — нет.
    std::optional<std::string> notice;
    /// Назначения LFO/ENV на ручки FX.
    std::map<std::string, ModAssign> fxAssigns;
    /// Мастер: RELEASE AUTO — MasterChain::autoValue.
    ParamMap master = MasterChain::defaultValues();
    bool satBypass = false;
    bool cmpBypass = false;
    bool clipBypass = false;
    bool limBypass = false;
    /// Громкость выхода мастера, dB.
    double outVol = 0;
    Eq masterEq;
    /// Свои последовательности MIX и MST.
    std::vector<Track> chanSeqs = {Track::channel("MIX"), Track::channel("MST")};
    std::vector<SceneRow> scenes = std::vector<SceneRow>(size_t(Params::patternSlots));
    /// Видимых строк.
    int rows = 1;
    int arrStart = 0;
    bool arrEndStop = false;
    bool plusCopy = false;
    /// Имена сэмплов (ключ → имя файла) и определённая высота.
    std::map<std::string, std::string> files;
    std::map<std::string, int> roots;
    bool rowLoop = true;
    bool arrowSwitch = false;
    bool gridHideSteps = false;
    /// Идёт переход MIX: последняя строка сетки — PREV.
    bool prevRow = false;
    Arrangement arrange;

    Project();

    /// Версия 9: ручка Filter — частота на всём ходу и тип фильтра (FilterType.swift; FilterType.cpp).
    void migrateFilter();

    // ProjectRepair.swift: ProjectRepair.cpp
    /// После чтения: миграции версий, размеры массивов и номера — в пределах.
    void repair();
    void migrateSwing();
    void migrateMidiIn();
    void migrateRandomStyles();
    static int clamp(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }
    /// Ключи сэмплов, на которые ссылается проект.
    std::set<std::string> sampleKeys() const;
    // SynthMigration.swift, GranularMigration.swift
    void migrateSynthKnobs();
    void migrateGranular();

    // TrackFx.swift: TrackFx.cpp
    /// FX дорожки (Reverb, Delay, Drive), а не общий (Duck).
    static bool trackFx(const std::string& id) { return id.compare(0, 3, "sc.") != 0; }
    /// Клип дорожки t, FX которого показывает и правит сетка FX; нет — −1.
    int fxClip(int t) const;
    double fxValue(int t, const std::string& id) const;
    void setFx(int t, const std::string& id, double v);
    void migrateTrackFx();

    // Клипы и строки (Clips.swift): Clips.cpp
    bool rowFilled(int k) const;
    /// Строк проекта: при переходе MIX последняя строка — PREV.
    int rowLimit() const { return prevRow ? Params::patternSlots - 1 : Params::patternSlots; }
    int visibleRows() const;
    /// Пустых ячеек нет: в видимых строках у каждой дорожки клип.
    void fillRows();
    bool canAddRow() const { return visibleRows() < rowLimit(); }
    /// «+» под строками: COPY PLAYING — копии клипов играющих (и слышимых) дорожек, остальным — пустые паттерны.
    void addRow();
    /// DUPLICATE строки: копия встаёт под ней, строки ниже сдвигаются вниз.
    void duplicateRow(int k);
    /// DELETE строки: её клипы убираются, строки ниже сдвигаются вверх; дорожка, игравшая её, останавливается.
    void deleteRow(int k);
    /// Перенос строки a на место b (строки между сдвигаются).
    void moveRow(int a, int b);
    /// Скопированная строка: клипы дорожек и сцена.
    struct RowClip {
        std::vector<std::optional<Clip>> clips;
        SceneRow scene;
    };
    RowClip copyRow(int k) const;
    void pasteRow(int k, const RowClip& clip);
    void setScene(int k, const std::string& name, int repeats);
    std::string sceneName(int k) const;
    /// Слышна ли дорожка (mute / solo); номер вне дорожек — не слышна.
    bool audible(int i) const;
    /// Гамма и нота клипа k дорожки t (Transition.swift); без своих — проекта.
    std::pair<int, int> clipKey(int t, int k) const;

    // Дорожки (TrackEdits.swift): TrackEdits.cpp
    /// Номера последовательностей MIX и MST в chanSeqs.
    static constexpr int mixSeq = 0;
    static constexpr int mstSeq = 1;
    bool canAddTrack() const { return int(tracks.size()) < Params::maxTracks; }
    bool canDeleteTrack(int i) const {
        return int(tracks.size()) > Params::trackCount && i >= 0 && i < int(tracks.size());
    }
    void addTrack();
    void deleteTrack(int i);
    void moveTrack(int from, int to);
    /// PASTE дорожки на место i: только настройки скопированной.
    void pasteTrack(int i, const Track& c);
    void renameTrack(int i, const std::string& name);
    /// Источники-дорожки динамических полос EQ (EqModel.swift): map — новый номер по старому, −1 — удалена.
    void remapEqDyn(const std::function<int(int)>& map);

    // Правки аранжировки (ArrangeEdits.swift): ArrangeEdits.cpp. Блок группы — (дорожка, номер блока).
    using BlockRef = std::pair<int, int>;
    int arrangeEnd() const;
    void pasteBlocks(int i, const std::vector<ArrBlock>& list);
    void deleteBlocks(const std::vector<BlockRef>& sel, bool closing = false);
    std::vector<BlockRef> moveBlocks(const std::vector<BlockRef>& sel, int d);
    std::vector<BlockRef> duplicateBlocks(const std::vector<BlockRef>& sel);
    void splitArrange(int t, int a, int b);
    void setTrackColor(int i, int color);
    int addMarker(int at, const std::string& name, std::optional<int> end = std::nullopt);
    int moveMarker(int k, int at);
    void setMarkerStart(int k, int x);
    void setMarkerEnd(int k, int x);
    void renameMarker(int k, const std::string& name);
    void setMarkerColor(int k, int color);
    void deleteMarker(int k);
    std::optional<int> section(int x) const;
    std::optional<int> sectionEnd(int k) const;
    /// Диапазон петли (границы в любом порядке); пустой — петля выключается.
    void setLoop(int a, int b);
    /// TO ARRANGE: строка k — с позиции at (nullopt — в конец); конец участка или nullopt.
    std::optional<int> rowToArrange(int k, std::optional<int> at, bool repeats = true);
    std::optional<std::pair<int, int>> placeRow(int k, const std::vector<int>& only, int reps, std::optional<int> at);
    static std::string markerName(const std::string& s);
    std::vector<std::pair<int, ArrBlock>> groupBlocks(const std::vector<BlockRef>& sel) const;
    void removeGroup(const std::vector<BlockRef>& sel);
    std::vector<BlockRef> blockIndices(const std::vector<std::pair<int, ArrBlock>>& list) const;

    // Запись аранжировки (ArrangeRecorder.swift): ArrangeRecorder.cpp.
    /// Записанные отрезки — в аранжировку (поздний заменяет ранний); played, fired, heard, live — запись «по факту».
    void applyRecording(const std::vector<ArrRecorder::Seg>& segs, const std::vector<std::map<int, int>>& played = {},
                        const std::vector<std::set<int>>& fired = {}, const std::vector<std::map<int, Step>>& heard = {},
                        const std::vector<ArrRecorder::LiveNote>& live = {});
    void placeFired(int t, const ArrRecorder::Seg& s, const Clip& src, const std::map<int, int>& played,
                    const std::set<int>& fired, const std::map<int, Step>& heard,
                    const std::vector<ArrRecorder::LiveNote>& live);
    void placeLive(int t, const ArrRecorder::Seg& s, const std::vector<ArrRecorder::LiveNote>& live);
    static bool addLive(Clip& c, const std::vector<ArrRecorder::LiveNote>& live, int from, int count, double level);
    /// Все блоки — со своей копией клипа.
    bool ownArrPatterns();

    // Правки EQ (EqModel.swift). Канал: дорожка 0… или EqSpec::masterChannel.
    Eq eq(int ch) const {
        if (ch == EqSpec::masterChannel) return masterEq;
        return ch >= 0 && ch < int(tracks.size()) ? tracks[size_t(ch)].eq : Eq();
    }
    template <typename F>
    void withEq(int ch, F&& f) {
        if (ch == EqSpec::masterChannel) f(masterEq);
        else if (ch >= 0 && ch < int(tracks.size())) f(tracks[size_t(ch)].eq);
    }
    /// Новая bell-полоса в первом свободном месте; −1, если мест нет.
    int addEqBand(int ch, double freq, double gain) {
        int k = -1;
        withEq(ch, [&](Eq& e) { k = e.addBand(freq, gain); });
        return k;
    }
    void setEqBand(int ch, int k, std::optional<double> freq, std::optional<double> gain = std::nullopt,
                   std::optional<double> q = std::nullopt) {
        withEq(ch, [&](Eq& e) { e.setBand(k, freq, gain, q); });
    }
    void setEqType(int ch, int k, int type) { withEq(ch, [&](Eq& e) { e.setType(k, type); }); }
    void setEqSlope(int ch, int k, int slope) { withEq(ch, [&](Eq& e) { e.setSlope(k, slope); }); }
    void setEqMs(int ch, int k, int ms) { withEq(ch, [&](Eq& e) { e.setMs(k, ms); }); }
    void setEqDyn(int ch, int k, int src) { withEq(ch, [&](Eq& e) { e.setDyn(k, src); }); }
    void setEqDynParams(int ch, int k, std::optional<double> thr, std::optional<double> range = std::nullopt) {
        withEq(ch, [&](Eq& e) { e.setDynParams(k, thr, range); });
    }
    void toggleEqBand(int ch, int k) { withEq(ch, [&](Eq& e) { e.toggleBand(k); }); }
    void deleteEqBand(int ch, int k) { withEq(ch, [&](Eq& e) { e.deleteBand(k); }); }
    /// BYPASS: выключение EQ канала.
    void toggleEq(int ch) { withEq(ch, [](Eq& e) { e.on = !e.on; }); }
    /// Ключ MORPH эффекта (TrackFx.swift): «fx:id» → id; nullopt — не эффект дорожки.
    static std::optional<std::string> morphFxId(const std::string& key);
    /// Эффект id дорожки t меняют в режиме MORPH: исходное — при первом изменении.
    void morphFx(int t, const std::string& id);
    /// MORPH на все дорожки на сетке FX: у остальных дорожек с клипом — сдвиг на ту же долю хода от их исходного.
    void morphShiftFx(int sel, const ParamDef& p, double norm);
    /// Вернуть всё, что меняли в режиме MORPH на дорожке i: эффекты — в клип, ручки и назначения — Track::undoMorph.
    void undoMorph(int i);
    /// Значение ключа MORPH на дорожке i сейчас (эффект — из клипа).
    std::optional<double> morphValue(int i, const std::string& key) const;
    void setMorphValue(int i, const std::string& key, double v);

    // MorphModes.swift: MorphModes.cpp
    /// MORPH на все дорожки: ручку id выбранной дорожки sel поставили на norm — у остальных дорожек, где эта ручка есть,
    /// — сдвиг на ту же долю хода от их значения на момент включения режима.
    void morphShift(int sel, const ParamDef& p0, double norm);
    /// UNDO / REDO не трогают MORPH: снимок получает от текущего проекта то, что накручено в режиме MORPH.
    void keepMorph(const Project& cur);
    /// Что вернуть при выключении MORPH: изменённые ручки всех дорожек.
    std::vector<MorphPoint> morphReturnPoints() const;
    /// Возврат на долю f (0…1): ручки — между значением при выключении и исходным (по ходу ручки).
    void applyMorphReturn(const std::vector<MorphPoint>& pts, double f);

    auto tie() const {
        return std::tie(version, bpm, scale, root, fx, tracks, notice, fxAssigns, master, satBypass, cmpBypass,
                        clipBypass, limBypass, outVol, masterEq, chanSeqs, scenes, rows, arrStart, arrEndStop, plusCopy,
                        files, roots, rowLoop, arrowSwitch, gridHideSteps, prevRow, arrange);
    }
    JAMBO_EQ(Project)
};

inline Project::Project() {
    for (int i = 0; i < Params::trackCount; ++i) tracks.emplace_back(i);
}

}  // namespace jambo
