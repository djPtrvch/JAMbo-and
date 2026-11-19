// Цепочка мастера (порт Core/Master/MasterChain.swift, ветка swift JAMbo): сумма дорожек и эффектов → EQ мастера →
// сатуратор → компрессор → клиппер → лимитер → громкость выхода. Параметры — плоский массив Double в порядке Index (из
// снимка звука или самотеста). BYPASS сатуратора — MIX 0, у остальных — свой кроссфейд 10 мс. Буферы — при создании.
// Отличие от Swift: модули — по указателям (заголовок лёгкий: его подключают модель проекта и справочники).
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "DynTele.h"
#include "MasterScope.h"
#include "../model/Params.h"
#include "eq/Spectrum.h"
#include "util/AtomicFloat.h"

namespace jambo {
struct Project;
class MasterSaturator;
class MasterGlue;
class MasterClipper;
class MasterLimiter;
class EqUnit;
class EqDynLevels;
class EqListen;
class RenderProfile;

class MasterChain {
public:
    /// Номера параметров в массиве значений колбэка.
    struct Index {
        static constexpr int satDrive = 0, satType = 1, satTone = 2, satMix = 3, satFocus = 4, satBypass = 5;
        static constexpr int cmpThr = 6, cmpAtt = 7, cmpRel = 8, cmpAuto = 9, cmpMakeup = 10, cmpRatio = 11;
        static constexpr int cmpKnee = 12, cmpMix = 13, cmpSchpf = 14, cmpBypass = 15;
        static constexpr int clipDrive = 16, clipCeiling = 17, clipSoftness = 18, clipBass = 19, clipBypass = 20;
        static constexpr int limGain = 21, limCeiling = 22, limLookahead = 23, limRel = 24, limAuto = 25;
        static constexpr int limBypass = 26;
        /// Громкость выхода — усиление; GUARD лимитера (0 — OFF).
        static constexpr int outGain = 27, limGuard = 28, count = 29;
    };

    /// Значения по умолчанию (bypass выключены).
    static constexpr double defaults[Index::count] = {
        0, 0, 0, 100, 20, 0,
        -10, 10, 400, 0, 0, 2, 6, 100, 90, 0,
        0, -1, 30, 0, 0,
        0, -1, 3, 150, 0, 0,
        1, 0,
    };
    /// RELEASE компрессора и лимитера ≥ autoValue — AUTO.
    static constexpr double autoValue = 1e9;
    /// ATTACK компрессора нового проекта — 30 мс.
    static constexpr double newAttack = 30;
    static constexpr double autoRel = 400;

    /// Параметры мастера в проекте (id) и их номера.
    static const std::vector<std::pair<std::string, int>>& ids();
    /// Значения мастера нового проекта.
    static ParamMap defaultValues();
    /// Значения проекта → массив параметров колбэка (при построении снимка звука).
    static void fill(double* p, const Project& project);

    explicit MasterChain(double sampleRate);
    ~MasterChain();
    MasterChain(const MasterChain&) = delete;
    MasterChain& operator=(const MasterChain&) = delete;

    std::unique_ptr<MasterSaturator> sat;
    std::unique_ptr<MasterGlue> glue;
    std::unique_ptr<MasterClipper> clip;
    std::unique_ptr<MasterLimiter> lim;
    std::unique_ptr<EqUnit> eq;
    EqDynLevels* eqDyn = nullptr;
    EqListen* eqListen = nullptr;
    DynTele compTele, clipTele, limTele;
    /// UNITY окна: MasterScope::clip или lim + 1, 0 — выключен.
    AtomicCounter unity;
    /// AUTO MAKEUP: поколение замера; итог — громкость входа и выхода без MAKEUP, dB.
    AtomicCounter measureGen;
    AtomicFloat measureIn{-240}, measureOut{-240};
    /// GUARD: на сколько dB убавлен вход лимитера.
    AtomicFloat limGuardCut{0};
    MasterScope scope;
    SpectrumTap satIn, satOut;
    AtomicCounter satOn;
    Spectra* spectra = nullptr;
    RenderProfile* profile = nullptr;

    /// Блок n отсчётов на месте; p — Index, eqParams — EQ мастера (nullptr — без EQ).
    void process(float* left, float* right, int n, const double* p, const double* eqParams = nullptr);
    /// UNITY: после метров и записи, до слышимого выхода.
    void applyUnity(float* l, float* r, int n);

private:
    long measureSeen = 0;
    float outCur = -1;
    float outK;
    float unityTarget = 1, unityCur = 1;
    float unityK;

    void applyOut(float* l, float* r, int n, float target);
};

}  // namespace jambo
