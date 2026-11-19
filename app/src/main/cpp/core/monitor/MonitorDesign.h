// Headphone Monitor (порт Core/Monitor/MonitorDesign.swift, ветка swift JAMbo): монитор-цепочка в конце выхода — только
// для прослушивания (не в экспорт, стемы и запись). Настройки, пресеты Translation и расчёт параметров для колбэка
// (MonitorChain) в основном потоке. Порядок: Mono → Room → Translation EQ и Sub-check → PHONE CURVE → CORRECT CURVE →
// Level compensation → Safety limiter.
// Отличие от Swift: настройки и профили — без словаря UserDefaults (хранение — на стороне Android).
#pragma once
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "eq/EqModel.h"

namespace jambo {

struct RoomResponse;

/// Профиль пункта SPACE: AMBIENCE, RAW, CORRECTION и CORRECT CURVE (JSON полос; пусто — без своей кривой).
struct MonitorProfile {
    double ambienceDb = -6;
    double raw = 1;
    double strength = 1;
    std::optional<std::string> correct;
};

/// Настройки монитор-цепочки (одни на приложение).
struct MonitorSettings {
    double strength = 1;
    std::string translation = "Studio CR7";
    bool mono = false;
    bool sub = false;
    double ambienceDb = -6;
    double raw = 1;
    std::map<std::string, std::shared_ptr<const MonitorProfile>> spaces;
    bool editCorrect = false;
    bool refMaster = false;
    bool refMonitor = true;
    bool refMatch = true;
    double refTrim = 0;
    bool bypassCorr = false;
    bool bypassSpace = false;
    std::string head = "FABIAN";
};

/// Константы цепочки.
namespace MonitorSpec {
constexpr double ambienceMin = -60;
inline double ambience(double db) { return db <= ambienceMin ? 0 : std::pow(10.0, db / 20); }
constexpr double filterMax = 12, compMax = 12, subHz = 120;
constexpr double ceilingDb = -0.5, hold = 0.03, release = 0.15;
constexpr int look = 64;
constexpr double fade = 0.025, glide = 0.025;
constexpr int transSlots = 8, subSlots = 2, corrSlots = 20, userSlots = 32;
constexpr int sections = transSlots + subSlots + corrSlots + userSlots;
constexpr int compAt = sections * 5, monoAt = compAt + 1, onAt = compAt + 2, size = compAt + 3;
constexpr double butter4[2] = {0.5411961001461969, 1.3065629648763766};
}  // namespace MonitorSpec

/// Пункт TRANSLATION (TranslationPresets.json): тембр (биквады), Mono и пространство.
struct TranslationPreset {
    struct Filter {
        std::string type;
        double freq = 0;
        std::optional<double> gain, q;
        std::optional<int> order;
    };
    std::string name;
    std::optional<bool> mono;
    std::vector<Filter> filters;
    std::optional<std::string> room;

    /// Список пресетов из JSON ({"presets": […]}); ошибка — пусто.
    static std::vector<TranslationPreset> list(const std::string& json);
};

/// Один биквад: тип EqSpec, частота, усиление, Q (линейный).
struct MonitorBiquad {
    int type;
    double freq, gain, q;
};

namespace MonitorDesign {
std::vector<MonitorBiquad> biquads(const TranslationPreset* p);
std::vector<MonitorBiquad> correction(const Eq* eq, double strength);
void coeffs(const MonitorBiquad& b, double sr, double* out);
inline void identity(double* o) {
    o[0] = 1;
    o[1] = o[2] = o[3] = o[4] = 0;
}
/// Параметры для колбэка (MonitorSpec::size значений).
std::vector<double> make(const Eq* eq, const MonitorSettings& m, const Eq* correct, const TranslationPreset* preset,
                         const RoomResponse* room, double ambience, double sampleRate);
double compensation(const double* p, double sr, const RoomResponse* room, double amb);
constexpr int points = 120;
inline double pointHz(int i) { return 20 * std::pow(1000.0, (double(i) + 0.5) / double(points)); }
inline bool isIdentity(const double* c) { return c[0] == 1 && c[1] == 0 && c[2] == 0 && c[3] == 0 && c[4] == 0; }
double power(const double* c, double w);
/// АЧХ всей цепочки (без компенсации и лимитера) на частоте hz для сигнала в центре, dB.
double response(const std::vector<double>& p, double hz, double sampleRate);
}  // namespace MonitorDesign

}  // namespace jambo
