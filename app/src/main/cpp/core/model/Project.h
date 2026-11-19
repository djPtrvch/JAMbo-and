// Модель проекта (порт Project и ProjectMore, Core/Model/Project.swift, ветка swift JAMbo). Значения: копия — снимок
// для UNDO. Поля ProjectMore («коробка» Swift) здесь — обычные поля проекта. Имена полей — ключи файла проекта.
#pragma once
#include <map>
#include <optional>
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
