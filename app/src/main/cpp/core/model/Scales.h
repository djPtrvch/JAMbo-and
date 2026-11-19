// Гаммы, виды аккордов, ноты клавиатуры и аккорды (порт Core/Model/Scales.swift, ветка swift JAMbo).
#pragma once
#include <string>
#include <vector>

namespace jambo {

struct Scale {
    std::string name;
    std::string shortName;  // short в Swift
    std::vector<int> steps;
};

namespace Scales {
extern const std::vector<Scale> all;
/// Типы аккорда CHORD и их ступени гаммы от ступени клавиши.
extern const std::vector<std::string> chordKinds;
extern const std::vector<std::vector<int>> chordDegrees;
/// Октава клавиатуры: 0…7, по умолчанию 3.
constexpr int octaveMin = 0;
constexpr int octaveMax = 7;
constexpr int octaveDef = 3;
/// Клавиш самое большее (две октавы хроматики + 1).
constexpr int maxKeys = 25;

int wrap(int v, int n);
/// Нижняя нота клавиатуры: корневая нота октавы.
int baseNote(int root, int octave);
/// Нота гаммы на ступени k от базовой (k может уходить в следующие октавы).
int note(int scale, int root, int octave, int degree);
/// Клавиш на экране: одна или две октавы гаммы + верхняя корневая нота.
int keyCount(int scale, int octaves);
/// Верхняя октава клавиатуры, при которой самая высокая нота (с CHORD — верх аккорда верхней клавиши) не выше 127.
int topOctave(int scale, int root, int octaves, bool chord, int kind = 0);
/// Аккорд ступени degree типа kind — ноты гаммы вверх от ноты ступени, без переноса.
std::vector<int> chordRaw(int scale, int root, int octave, int degree, int kind);
/// Аккорд клавиши: нота выше 127 — на октаву ниже.
std::vector<int> chord(int scale, int root, int octave, int degree, int kind = 0);
}  // namespace Scales

}  // namespace jambo
