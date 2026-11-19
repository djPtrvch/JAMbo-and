// Движки синта: звук дорожки или шага «mi:<n>» (порт Core/Model/SynthEngines.swift, ветка swift JAMbo).
// Свои движки KICK, ANALOG, FM, WTABLE (16–19), ударные и SUB (20–24), TEXTURE и GRANULAR (25, 26).
// Track.setSynth — при переносе модели проекта.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace jambo {

/// Движок: номер, короткое имя (меню), полное (окно синта), подписи ручек 1.1–1.4, пункты ступенчатых ручек
/// (пустой список — плавная). decay — затухает сам; decayMax — свой предел Decay, мс; initKnobs — ручки 1.1–1.4 при
/// выборе движка на дорожку с другого звука.
struct SynthEngine {
    int id;
    std::string name;
    std::string full;
    std::vector<std::string> knobs;
    std::vector<std::vector<std::string>> options;
    bool decay = false;
    std::optional<double> decayMax;
    std::vector<double> initKnobs;
};

namespace SynthEngines {
/// Ручки 1.1–1.4 синта в проекте.
extern const std::vector<std::string> knobIds;
const std::vector<SynthEngine>& all();
/// Колонки меню звука: DRUMS и SYNTH.
extern const std::vector<int> drums;
extern const std::vector<int> synths;
/// Размер таблиц по номеру движка (наибольший номер + 1).
int count();
/// Движок по номеру; nullptr — номер не занят.
const SynthEngine* engine(int id);
std::string capitalized(const std::string& s);
}  // namespace SynthEngines

/// Ключ звука синта: «mi:<n>» — движок n.
namespace SynthKey {
std::string key(int engine);
/// Номер движка из ключа звука; не синт или неизвестный номер — −1.
int engine(const std::string& key);
}  // namespace SynthKey

}  // namespace jambo
