// TEXTURE (25): непрерывный стереошум (подложка) и поверх — треск Grit (пуассоновский поток щелчков со случайной
// панорамой); путь Color (ФНЧ → ФВЧ) и Tone (четыре полосовых на ноте·ra[i], Metal — инхармонические отношения).
// Порт 1:1 Core/Jambo/TextureVoice.swift (ветка swift JAMbo); поведение — docs/ai/synth-engines.md JAMbo.
// Ручки 0…1: 1.1 Color, 1.2 Grit, 1.3 Tone, 1.4 Metal. Монофоничен; новая нота не перезапускает шум.
#pragma once
#include <cstdint>

namespace jambo {

namespace TextureMap {
constexpr int maxEvents = 150;
constexpr double width = 0.7, bed = 0.9, out = 0.63;
double rate(double g);
double length(double g);
double gain(double g);
double lpHz(double c, double sr);
double hpHz(double c);
double q(double tone);
double weight(int i, double c);
double ratio(int i, double m);
const char* colorName(double c);
}  // namespace TextureMap

class TextureVoice {
public:
    static constexpr int maxChunk = 256;
    explicit TextureVoice(double sampleRate, int seed = 1);

    /// Ручки (0…1) и нота (MIDI).
    void set(double note, double q1, double q2, double q3, double q4);
    /// Нота: поток щелчков не шёл — первый через 3 мс; уже идёт — продолжается.
    void trigger(double note, double q1, double q2, double q3, double q4);
    /// Голос молчал: щелчков и состояния фильтров нет, сглаженные значения встают сразу.
    void silence();
    /// Стерео-выход n кадров (n ≤ maxChunk); window — кадров, в которых ещё рождаются щелчки (INT32_MAX — держится).
    void render(float* outL, float* outR, int n, int window);
    int events() const { return events_; }

private:
    double u();
    void coef(int f, double hz, double q);
    void targets(double k);
    void spawn(int delay, double rate, double len, double gain);
    void filter(float* outL, float* outR, int n, double prevDry, double prevWet);

    double sr_;
    double note_ = 57;
    double color_ = 0.4, grit_ = 0.3, tone_ = 0.3, metal_ = 0;
    int64_t rnd_ = 1, nC_ = 7, nL_ = 11, nR_ = 13;
    double sLp_ = 1000, sHp_ = 30, sMk_ = 1, sDry_ = 1, sWet_ = 0, sQ_ = 10;
    double sF_[4] = {220, 220, 220, 220};
    double sW_[4] = {};
    double co_[30] = {};
    double zs_[24] = {};
    bool fresh_ = true;
    double eEnv_[TextureMap::maxEvents] = {}, eAmp_[TextureMap::maxEvents] = {};
    int ePos_[TextureMap::maxEvents] = {}, eLen_[TextureMap::maxEvents] = {};
    double eK_[TextureMap::maxEvents] = {}, eL_[TextureMap::maxEvents] = {}, eR_[TextureMap::maxEvents] = {};
    int64_t eRnd_[TextureMap::maxEvents];
    int events_ = 0;
    double nextAt_ = 0;
    bool spawning_ = false;
    double busL_[maxChunk] = {}, busR_[maxChunk] = {};
};

}  // namespace jambo
