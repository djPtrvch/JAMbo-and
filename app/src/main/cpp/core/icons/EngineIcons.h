// Иконки (превью) своих движков (порт Core/Icons/EngineIcons*.swift, ветка swift JAMbo). Поле 64×64;
// elements(engine, v) — чистая функция ручек 1.1–1.4 и Decay 2.2 (0…1). Рисует экран двумя цветами темы: основной —
// сам звук, вторичный (серый) — то, что его формирует.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace jambo {

class WtTable;

/// Значения иконки 0…1: h — ручка 1.1, t — 1.2, m — 1.3, q — 1.4, d — Decay 2.2; a — флаг (не задаётся).
struct IconValues {
    double h = 0.5, t = 0.5, m = 0.5, q = 0.5, d = 0.5;
    bool a = false;
    /// ANALOG: режим модуляции (AnalogMod: 0 — нет, 1 SYNC, 2 FM, 3 RING, 4 AM) и октава B ниже.
    int am = 0;
    bool ao = false;
    bool operator==(const IconValues& o) const {
        return std::tie(h, t, m, q, d, a, am, ao) == std::tie(o.h, o.t, o.m, o.q, o.d, o.a, o.am, o.ao);
    }
    bool operator!=(const IconValues& o) const { return !(*this == o); }
};

/// Команда контура (подмножество SVG path, как в источнике): move, line (x, y); hTo, vTo, hBy, vBy (x); close.
/// arc и arcBy в нынешних иконках не встречаются (в Swift есть для старых форм).
struct IconCmd {
    enum Kind { move, line, hTo, vTo, hBy, vBy, close } kind;
    double x = 0, y = 0;
    static IconCmd mv(double x, double y) { return {move, x, y}; }
    static IconCmd ln(double x, double y) { return {line, x, y}; }
    static IconCmd cl() { return {close, 0, 0}; }
    bool operator==(const IconCmd& o) const { return kind == o.kind && x == o.x && y == o.y; }
};

/// Прямоугольник колонки на поле 64×64 (x, y — левый верхний угол).
struct IconRect {
    double x = 0, y = 0, w = 0, h = 0;
    bool operator==(const IconRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

/// Цвет элемента: основной (сам звук) или вторичный (серый: ось, хвост Decay, фильтр, модулятор).
enum class IconInk { primary, secondary };

/// Элемент иконки: контур, окружность (cx, cy, r), текст или колонки (прямоугольники одним путём, заливка).
struct IconElement {
    std::vector<IconCmd> cmds;
    std::vector<IconRect> rects;
    IconInk ink = IconInk::primary;
    std::optional<std::tuple<double, double, double>> circle;
    std::optional<std::string> text;
    double textX = 0, textY = 0, textSize = 10;
    bool fill = false;
    std::optional<double> opacity;
    bool dash = false;
    /// Волна генератора — сплошная фигура по всем точкам сигнала (wave — верхний край, waveBot — нижний),
    /// contour — столбики огибающей сглаженным контуром.
    std::vector<double> wave, waveBot;
    double waveX0 = 4, waveX1 = 60;
    bool contour = false;

    static IconElement path(std::vector<IconCmd> c, std::optional<double> op = std::nullopt, bool fill = false,
                            bool dash = false, IconInk ink = IconInk::primary);
    static IconElement ofRects(std::vector<IconRect> r, IconInk ink = IconInk::primary,
                               std::optional<double> op = std::nullopt);
    static IconElement ofText(std::string s, double x, double y, double size);
};

/// Колонка lane прототипа: ось y0, размах amp; mirror — симметрично оси (огибающая), thin — без минимальной толщины.
struct IconLane {
    double y0 = 32, amp = 1;
    bool mirror = false, thin = false;
    double alpha = 1;
    IconInk ink = IconInk::primary;
    double x0 = 4, x1 = 60;
    std::function<double(double)> ext;
};

namespace EngineIcons {
using Frame = std::function<double(double, double)>;
using Elements = std::vector<IconElement>;

constexpr double stroke = 1.4;
constexpr double size = 64;
constexpr double tau = 6.283185307179586;
constexpr int cols = 72, sub = 6;
constexpr double ns = 432.0, x0 = 4.0, x1 = 60.0, still = 1.3;

/// Элементы иконки своего движка (16–26); у других — пусто. hx — центр подсветки удара (только у ударных).
Elements elements(int engine, const IconValues& v, std::optional<double> hx = std::nullopt);

// Колонки (EngineIconsColumns.swift)
inline double clamp(double v, double a = 0, double b = 1) { return v < a ? a : (v > b ? b : v); }
inline double lerp(double a, double b, double u) { return a + (b - a) * u; }
double hash(double n);
double rnd(int i, double seed);
double nz(int i);
double nzi(double x);
double sstep(double t);
double frac(double p);
double shSine(double p);
double shTri(double p);
double shSaw(double p);
double shSq(double p);
double shape(int k, double p);
double morph(const std::vector<int>& kinds, double idx, double p);
std::vector<double> gen(const std::function<double(double, int)>& fn, int cols = 72);
IconElement lane(const std::vector<double>& a, const IconLane& o, std::optional<double> hx = std::nullopt);
IconElement axis(double y, double xa = 4, double xb = 60);
std::function<double(double)> startSpike(double k, double w);

// Ударные (EngineIconsDrums.swift)
double drumEnv(double u, double d);
Elements drumBurst(double y0, double amp, double a, double w);
Elements kickIcon(const IconValues& v, std::optional<double> hx);
Elements snareIcon(const IconValues& v, std::optional<double> hx);
Elements percIcon(const IconValues& v, std::optional<double> hx);
Elements hatIcon(const IconValues& v, std::optional<double> hx);
Elements clapIcon(const IconValues& v, std::optional<double> hx);

// Синты (EngineIconsJambo.swift)
Elements analogIcon(const IconValues& v);
Elements fmIcon(const IconValues& v);
Elements subIcon(const IconValues& v);
double basicFrame(double pos, double p);
Elements wtableIcon(const IconValues& v, const Frame& frame);
/// Кадр таблицы дорожки: уровень 0 мипмапа, между кадрами и отсчётами — линейно.
Frame tableFrame(const WtTable& tb);

// TEXTURE и GRANULAR (EngineIconsTexGran.swift)
constexpr double texWindow = 0.6;
std::vector<double> iconEvents(double rate, double w, double seed, int max = 150);
Elements textureIcon(const IconValues& v);
/// peaks — огибающая сэмпла SOURCE (433 точки 0…1); пусто — условный сплошной файл.
Elements granularIcon(const IconValues& v, const std::vector<double>* peaks = nullptr);
}  // namespace EngineIcons

namespace TexGranIcon {
double smooth(double a, double b, double x);
double sampleEnv(double p);
}  // namespace TexGranIcon

}  // namespace jambo
