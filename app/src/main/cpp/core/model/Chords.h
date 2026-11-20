// Названия аккордов по интервалам и римские ступени (режим CHORD; порт Core/Model/Chords.swift, ветка swift JAMbo).
// Случайная прогрессия (SHIFT + CHORD) и writeProgression — вместе со случайными партиями, позже.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jambo {
namespace Chords {
/// Аккордов в прогрессии.
constexpr int count = 4;
/// Начала и длины аккордов прогрессии на pages страницах по 16 шагов: 4 аккорда поровну.
std::vector<std::pair<int, int>> places(int pages);
/// Индекс ступени гаммы (0…n−1) для номера ступени (1…7; гамма короче — по модулю).
int degreeIndex(int degree, int scale);
/// Название аккорда по интервалам от нижней ноты (kind — тип): Em, F, B°, Cmaj7, Dm7b5, G9…; иначе «E(sus2)».
std::string name(const std::vector<int>& notes, int kind);
std::optional<std::string> quality(const std::vector<int>& iv, int kind);
std::optional<std::string> triadName(int third, int fifth);
std::optional<std::string> seventhName(int third, int fifth, int seventh);
std::optional<std::string> ninthName(const std::string& seventh, int ninth);
/// Римская ступень (degree — индекс в гамме): малая терция — строчные, «°» — уменьшённое, «+» — увеличенное.
std::string numeral(int degree, const std::vector<int>& notes);
/// Подпись клавиши в режиме CHORD: «Em · i».
std::string keyLabel(int scale, int root, int octave, int degree, int kind);
/// Аккорды ступеней (номера 1…7).
std::vector<std::vector<int>> chords(const std::vector<int>& degrees, int scale, int root, int octave, int kind);
}  // namespace Chords
}  // namespace jambo
