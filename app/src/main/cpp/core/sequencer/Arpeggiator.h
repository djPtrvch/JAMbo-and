// Арпеджиатор в колбэке (порт Core/Sequencer/Arpeggiator.swift, ветка swift JAMbo): живые ноты дорожки с включённым ARP
// не звучат сами — играют по одной, по сетке транспорта (при игре) или от нажатия по темпу. Без выделения памяти в
// колбэке. Ноты блока — в список событий (emit*), их отдаёт сэмплеру AudioCore.
#pragma once
#include <cstdint>
#include <vector>
#include "SeqData.h"
#include "util/SPSCQueue.h"

namespace jambo {

/// Нота арпеджио для записи: кадр начала, длина в кадрах и ход транспорта блока.
struct ArpOut {
    int32_t track = 0;
    int32_t note = 0;
    float velocity = 1;
    double frame = 0;
    double gateFrames = 0;
    bool playing = false;
    int nextTick = 0;
    double nextFrame = 0;
    double stepFrames = 6000;
};

class Arpeggiator {
public:
    static constexpr int maxHeld = 16, maxRow = 64, maxEmit = 128;
    /// liveId нот арпеджио дорожки t.
    static int noteId(int t) { return -1000000000 - t; }

    Arpeggiator();
    bool active(int t) const { return t >= 0 && t < tracks && on[size_t(t)]; }
    void configure(int t, bool o, int d, int r, int oc, double g, bool l, int64_t frame);
    void press(int t, int n, int id, float v, int64_t frame);
    void release(int t, int id, int64_t frame);
    /// Блок [start, start + frames): ноты дорожек с ARP.
    void process(int64_t start, int frames, bool playing, int nextTick, double nextFrame, double stepFrames);
    void clearEmit() { emitCount = 0; }

    int emitCount = 0;
    std::vector<int32_t> emitKind, emitTrack, emitNote;
    std::vector<float> emitVel;
    std::vector<int64_t> emitFrame;
    SPSCQueue<ArpOut> out{512, ArpOut()};

private:
    const int tracks = SeqData::maxTracks;
    std::vector<uint8_t> on;
    std::vector<int32_t> dir;
    std::vector<double> rate;
    std::vector<int32_t> octaves;
    std::vector<double> gate;
    std::vector<uint8_t> latch;
    std::vector<int32_t> heldNote;
    std::vector<float> heldVel;
    std::vector<int> heldId;
    std::vector<uint8_t> heldPressed;
    std::vector<int32_t> heldCount;
    std::vector<uint8_t> running;
    std::vector<double> nextAt;
    std::vector<int32_t> index;
    std::vector<uint8_t> sounding;
    std::vector<int64_t> offAt;
    std::vector<int32_t> lastNote;
    std::vector<int32_t> row;
    std::vector<float> rowVel;
    uint64_t rng = 0x9E3779B97F4A7C15ull;

    void dropReleased(int t, int64_t frame);
    void stop(int t, int64_t frame);
    void emit(int32_t kind, int t, int32_t n, float v, int64_t f);
    int buildRow(int t);
    int pick(int d, int k, int len);
};

}  // namespace jambo
