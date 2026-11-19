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
#include "../eq/EqModel.h"
#include "../master/MasterChain.h"

namespace jambo {

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
