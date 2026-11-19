// Мастер: константы, нужные справочникам (порт части enum MasterChain, Core/Master/MasterChain.swift, ветка swift
// JAMbo); остальное — при переносе мастера.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "../model/Params.h"

namespace jambo {
struct Project;
namespace MasterChain {

/// Значения проекта → массив параметров колбэка (count значений; при построении снимка звука). MasterChain.cpp
void fill(double* p, const Project& project);

/// RELEASE компрессора и лимитера ≥ autoValue — AUTO.
constexpr double autoValue = 1e9;
/// ATTACK компрессора нового проекта — 30 мс.
constexpr double newAttack = 30;

/// Номера параметров в массиве значений колбэка.
namespace Index {
constexpr int satDrive = 0, satType = 1, satTone = 2, satMix = 3, satFocus = 4, satBypass = 5;
constexpr int cmpThr = 6, cmpAtt = 7, cmpRel = 8, cmpAuto = 9, cmpMakeup = 10, cmpRatio = 11, cmpKnee = 12;
constexpr int cmpMix = 13, cmpSchpf = 14, cmpBypass = 15;
constexpr int clipDrive = 16, clipCeiling = 17, clipSoftness = 18, clipBass = 19, clipBypass = 20;
constexpr int limGain = 21, limCeiling = 22, limLookahead = 23, limRel = 24, limAuto = 25, limBypass = 26;
constexpr int outGain = 27, limGuard = 28, count = 29;
}  // namespace Index

/// Значения по умолчанию (bypass выключены).
constexpr double defaults[Index::count] = {
    0, 0, 0, 100, 20, 0,
    -10, 10, 400, 0, 0, 2, 6, 100, 90, 0,
    0, -1, 30, 0, 0,
    0, -1, 3, 150, 0, 0,
    1, 0,
};

/// Параметры мастера в проекте (id) и их номера.
inline const std::vector<std::pair<std::string, int>>& ids() {
    static const std::vector<std::pair<std::string, int>> v = {
        {"sat.drive", Index::satDrive}, {"sat.type", Index::satType}, {"sat.tone", Index::satTone},
        {"sat.mix", Index::satMix}, {"sat.focus", Index::satFocus},
        {"cmp.threshold", Index::cmpThr}, {"cmp.attack", Index::cmpAtt}, {"cmp.release", Index::cmpRel},
        {"cmp.makeup", Index::cmpMakeup}, {"cmp.ratio", Index::cmpRatio}, {"cmp.knee", Index::cmpKnee},
        {"cmp.mix", Index::cmpMix}, {"cmp.schpf", Index::cmpSchpf},
        {"clip.drive", Index::clipDrive}, {"clip.ceiling", Index::clipCeiling},
        {"clip.softness", Index::clipSoftness}, {"clip.bass", Index::clipBass},
        {"lim.input", Index::limGain}, {"lim.ceiling", Index::limCeiling}, {"lim.lookahead", Index::limLookahead},
        {"lim.release", Index::limRel}, {"lim.guard", Index::limGuard},
    };
    return v;
}

/// Значения мастера нового проекта.
inline ParamMap defaultValues() {
    ParamMap d;
    for (const auto& p : ids()) d[p.first] = defaults[p.second];
    d["cmp.attack"] = newAttack;
    return d;
}

}  // namespace MasterChain
}  // namespace jambo
