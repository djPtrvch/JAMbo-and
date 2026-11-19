// Запись стемов (порт Core/Engine/StemRing.swift, ветка swift JAMbo): колбэк кладёт звук пишущихся каналов (дорожки,
// мастер, REC редактора) в одно кольцо без потерь, сообщения о блоках, начале и конце — в очередь в том же порядке;
// основной поток (StemCollector) забирает их по таймеру. Кольцо выделяется при первом нажатии ● (prepare).
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "sampler/SampleData.h"
#include "sequencer/SeqData.h"
#include "util/SPSCQueue.h"

namespace jambo {

/// Сообщение колбэка о записи стема.
struct StemMsg {
    enum class Kind : uint8_t { none, started, data, ended, endAt };
    Kind kind = Kind::none;
    int channel = 0;
    /// data — кадров в блоке; ended — кадров в стеме; endAt — кадр конца записи (−1 — не виден).
    int64_t value = 0;
    /// data — начало блока в кольце; ended — пропущено кадров (кольцо было полно).
    long pos = 0;
};

class StemRing {
public:
    /// Каналы: дорожки 0…maxTracks−1, мастер и REC редактора сэмпла.
    static constexpr int channels = SeqData::maxTracks + 2;
    static constexpr int master = SeqData::maxTracks;
    static constexpr int capture = SeqData::maxTracks + 1;
    /// Отсчётов в кольце (стерео подряд).
    static constexpr long capacity = 1L << 22;
    /// Кадров в блоке самое большее (Sampler.maxFrames).
    static constexpr int maxFrames = 4096;
    static constexpr int64_t never = std::numeric_limits<int64_t>::max();

    StemRing();

    SPSCQueue<StemMsg> msgs{16384, StemMsg()};
    /// Гейт mute/solo дорожек по отсчётам блока (дорожка·maxFrames): пишет микшер, пока идёт запись.
    std::vector<float> gate;
    /// Пишется ли хоть один канал.
    bool capturing() const { return capturing_; }
    /// Источник REC (канал capture): дорожка или master; −1 — нет.
    int captureSource = -1;

    /// Основной поток, при нажатии ●: выделить кольцо (один раз).
    void prepare();
    bool isReady() const { return ready.load(std::memory_order_acquire); }

    // Колбэк
    void begin(int ch, int64_t at, int64_t limit = never);
    void end(int ch, int64_t at);
    bool isActive(int ch) const { return active[size_t(ch)] != 0; }
    void endAt(int ch, int64_t frame) { msgs.push(StemMsg{StemMsg::Kind::endAt, ch, frame, 0}); }
    /// Блок канала [blockStart, blockStart + n): l·g, r·g (g — гейт или nullptr) — часть внутри окна записи.
    void captureBlock(int ch, const float* l, const float* r, const float* g, int64_t blockStart, int n);

    // Основной поток
    /// Скопировать k кадров с позиции pos в l и r и освободить место в кольце.
    void read(long pos, int k, std::vector<float>& l, std::vector<float>& r);

private:
    std::unique_ptr<float[]> buf;
    std::atomic<bool> ready{false};
    std::atomic<long> written{0}, readTotal{0};
    std::vector<uint8_t> active;
    std::vector<int64_t> startF, endF, count;
    std::vector<long> dropped;
    bool capturing_ = false;
};

/// Основной поток: собирает стемы из сообщений кольца (по таймеру).
class StemCollector {
public:
    struct Done {
        int channel;
        SampleRef data;
        long dropped;
    };
    StemCollector() : left(StemRing::channels), right(StemRing::channels) {}
    const std::set<int>& recording() const { return recording_; }
    const std::map<int, int64_t>& endAt() const { return endAt_; }
    const std::vector<float>& bufferL(int ch) const { return left[size_t(ch)]; }
    const std::vector<float>& bufferR(int ch) const { return right[size_t(ch)]; }
    /// Готовые стемы: канал, звук и пропущено ли что-то.
    std::vector<Done> poll(StemRing& ring);

private:
    std::vector<std::vector<float>> left, right;
    std::set<int> recording_;
    std::map<int, int64_t> endAt_;
};

}  // namespace jambo
