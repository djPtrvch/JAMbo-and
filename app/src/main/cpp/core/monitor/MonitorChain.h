// Монитор-цепочка в колбэке (порт Core/Monitor/MonitorChain.swift, ветка swift JAMbo): параметры — MonitorDesign::make в
// основном потоке, через очередь номеров заранее выделенных буферов. Биквады — DF2T в Double; плавно: коэффициенты — к
// новым за ~25 мс, компенсация — линейно в блоке, Mono и вкл./выкл. — кроссфейд 25 мс. Комната — RoomStage. Нейтральная
// цепочка — выход бит в бит равен входу. Safety limiter с упреждением 64 отсчёта.
#pragma once
#include <vector>
#include "MonitorDesign.h"
#include "RoomConv.h"
#include "util/AtomicFloat.h"
#include "util/SPSCQueue.h"

namespace jambo {

class MonitorChain {
public:
    static constexpr int slots = 8;
    static constexpr int maxFrames = 4096;
    explicit MonitorChain(double sampleRate);
    const double sampleRate;
    /// Экспорт песни идёт (1): цепочка не применяется.
    AtomicCounter paused;
    RoomStage room;
    /// Наибольшее снижение лимитером в последнем блоке (1 — нет).
    AtomicFloat limitGain{1};

    /// Новые параметры (основной поток; MonitorSpec::size значений).
    void publish(const std::vector<double>& p);
    /// Блок на месте (колбэк). raw — слышен референс мимо цепочки: цепочка уходит.
    void process(float* left, float* right, int frames, bool raw);

private:
    std::vector<double> ring;
    SPSCQueue<int> queue{16, -1};
    int nextSlot = 0;
    std::vector<double> tgt, cur, state, wl, wr;
    double comp = 1, mono = 0, wet = 0;
    std::vector<double> ldl, ldr, lneed, lbox;
    int lpos = 0;
    double limit = 1;
    int holdLeft = 0;
    int holdN;
    double fadeStep, release, ceiling;

    bool neutral() const;
    void glide(int n);
    void processMono(int n);
    void section(int k, int n);
    void processGain(int n);
    void output(float* left, float* right, int n, double target);
};

}  // namespace jambo
