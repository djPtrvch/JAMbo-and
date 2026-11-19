// Единая таблица параметров: подписи ручек, диапазоны, значения по умолчанию, форматы
// (порт Core/Model/Params.swift, ветка swift JAMbo). Меняем подписи и диапазоны только здесь.
// Не для аудиоколбэка: строки, std::function, таблицы строятся при первом обращении.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jambo {

/// Значения ручек по id (звук дорожки, FX, мастер).
using ParamMap = std::map<std::string, double>;

enum class KnobKind { smooth, stepped };

struct ParamDef {
    using Format = std::function<std::string(double)>;

    std::string id;
    std::string label;
    KnobKind kind = KnobKind::smooth;
    double min = 0, max = 1, def = 0;
    /// Степенная шкала от нуля: значение = min + (max − min)·n^curve.
    std::optional<double> curve;
    /// Экспоненциальная шкала (частоты).
    bool exp = false;
    /// Дуга от центра (Pan, Pitch, Filter).
    bool bipolar = false;
    /// Подписи пунктов ступенчатой ручки (значение = индекс).
    std::vector<std::string> options;
    /// Пункты ступенчатой ручки с непрерывным значением: значение — середина пункта на min…max.
    std::optional<int> steps;
    /// Шаг значения (0,1 dB у мастера).
    std::optional<double> step;
    /// Последняя доля хода — AUTO (MasterChain::autoValue).
    std::optional<double> autoTop;
    /// Первая доля хода — OFF (значение 0).
    std::optional<double> offBottom;
    /// P-lock на ручку не ставится (ручки мастера).
    bool noLock = false;
    Format format;

    ParamDef() = default;
    ParamDef(std::string id_, std::string label_, double min_, double max_, double def_, Format f)
        : id(std::move(id_)), label(std::move(label_)), min(min_), max(max_), def(def_), format(std::move(f)) {}

    // Необязательные поля — цепочкой (вместо параметров по умолчанию Swift).
    ParamDef& withKind(KnobKind k) { kind = k; return *this; }
    ParamDef& withCurve(double c) { curve = c; return *this; }
    ParamDef& withExp(bool e = true) { exp = e; return *this; }
    ParamDef& withBipolar(bool b = true) { bipolar = b; return *this; }
    ParamDef& withOptions(std::vector<std::string> o) { options = std::move(o); return *this; }
    ParamDef& withSteps(int s) { steps = s; return *this; }
    ParamDef& withStep(double s) { step = s; return *this; }
    ParamDef& withAutoTop(double a) { autoTop = a; return *this; }
    ParamDef& withOffBottom(double o) { offBottom = o; return *this; }
    ParamDef& withNoLock(bool n = true) { noLock = n; return *this; }

    /// Значение → положение ручки 0…1.
    double toNorm(double v) const;
    /// Положение ручки 0…1 → значение (ступенчатые округляются).
    double fromNorm(double n0) const;

private:
    /// Та же шкала без AUTO, OFF и шага.
    ParamDef plain() const;
};

namespace Fmt {
std::string pct(double v);
std::string ms(double v);
std::string hz(double v);
/// Частота ручки Filter: 0…100 — 20 Гц … 20 кГц по экспоненте.
double filterHz(double v);
/// Фильтр открыт: LP и LADDER — у правого края, HP — у левого; у BP — никогда.
bool filterOpen(double v, int type);
/// «LP 1.2k», открытый — «LP OFF».
std::string filter(double v, int type);
std::string pan(double v);
std::string pitch(double v);
}  // namespace Fmt

/// Пункт со временем в четвертях (LFO по темпу, Delay).
struct Div {
    const char* name;
    double beats;
};

namespace Params {

constexpr int stepsPerPage = 16;
constexpr int maxPages = 8;
constexpr int trackCount = 8;
constexpr int patternSlots = 16;
constexpr int gridRows = 8;
constexpr int maxTracks = 16;
constexpr double bpmMin = 40.0;
constexpr double bpmMax = 240.0;
/// Сэмпл звучит в исходной высоте на C3.
constexpr int sampleRoot = 48;
/// Громкость дорожки в микшере, dB.
constexpr double volMin = -60.0;
constexpr double volDef = -10.0;

double volGain(double db);
std::string volText(double v);
const std::vector<ParamDef>& mixGrid();
/// Дорожка ручки громкости MIX по id (mix.vol1 → 0), иначе −1.
int mixTrack(const std::string& id);
std::string noteName(int n);

/// Пустая ячейка сетки (id ""): неактивная серая ручка.
const ParamDef& empty();
/// Swing дорожки: ползунок в меню клавиатуры.
const ParamDef& swing();

const std::vector<std::vector<ParamDef>>& trackColumns();
const ParamDef& rate();
const ParamDef& rateTstr();
extern const char* const rateModeId;
std::string stepModeId(const std::string& id);
const ParamDef& pitchSemi();
const ParamDef& startStep();
const ParamDef& endStep();
const ParamDef& filterLp();
const ParamDef& filterHp();
const ParamDef& filterBp();
const ParamDef& filterLadder();
extern const char* const filterTypeId;
const ParamDef& filterDef(int type);
const std::vector<ParamDef>& trackGrid();
const std::vector<ParamDef>& synthColumn();
int stepIndex(double n, int steps);
/// Сетка синта по номеру движка (пустая у незанятого номера).
const std::vector<std::vector<ParamDef>>& synthGrids();
/// Сетка ручек дорожки по её звуку: синт — сетка движка, иначе сэмплер.
const std::vector<ParamDef>& grid(const std::string& sound);

// MOD
extern const std::vector<std::string> lfoWaves;
extern const std::vector<Div> lfoDivs;
extern const std::vector<std::string> lfoSyncIds;
constexpr double lfoSpeedMin = 0.05;
constexpr double lfoSpeedMax = 20.0;
int lfoDivIndex(double v);
ParamDef lfoSpeedSync(int n);
std::string lfoHz(double v);
std::vector<ParamDef> lfoColumn(int n);
const std::vector<std::vector<ParamDef>>& modColumns();
const std::vector<ParamDef>& modGrid();
const std::vector<std::string>& modIds();
constexpr int modCount = 16;

// FX
extern const std::vector<Div> delayDivs;
constexpr double driveLowCutOff = 25.0;
constexpr double driveLowCutNew = 80.0;
constexpr double revHpfNew = 120.0;
/// FILTER дилея: тип (−1 LP, 0 выкл., 1 HP) и частота.
std::pair<int, double> delayFilterFreq(double v);
const std::vector<std::vector<ParamDef>>& fxColumns();
const std::vector<ParamDef>& fxGrid();
const std::vector<ParamDef>& hiddenFx();
extern const std::vector<std::string> fxIds;
constexpr int fxCount = 18;
constexpr double dlyFreeMin = 10.0;
constexpr double dlyFreeMax = 2000.0;
constexpr double dlyFreeTop = 11.0;
double dlyFreeMs(double v);
double dlyFreeValue(double ms);
const ParamDef& dlyTimeFree();
int nearestDelayDiv(double ms, double bpm);
ParamMap fxDefaults();

// Мастер
std::string masterDb(double v);
std::string masterMs(double v);
extern const std::vector<std::string> satTypes;
const std::vector<std::vector<ParamDef>>& masterColumns();
std::string masterHz(double v);
const std::vector<ParamDef>& masterExtra();
const std::vector<ParamDef>& masterGrid();

/// Описание по id (nullptr — нет такого).
const ParamDef* byId(const std::string& id);
ParamMap defaults();

}  // namespace Params
}  // namespace jambo
