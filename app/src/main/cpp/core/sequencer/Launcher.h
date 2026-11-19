// Запуск клипов и строк в колбэке (порт Core/Sequencer/Launcher.swift и ArrangePlay.swift, ветка swift JAMbo):
// ожидающий клип стартует, когда текущий доиграет (у остановленной — с начала такта); строка — с начала такта
// (SWITCH — в конце прохода); повторы строк, LOOP ROW, AT THE END; аранжировка — дорожки по таймлайну.
#pragma once
#include <memory>
#include <vector>
#include "SeqData.h"
#include "SeqOut.h"
#include "util/SPSCQueue.h"

namespace jambo {

class Launcher {
public:
    /// Нет ожидания / ожидает остановки (иначе — номер ячейки).
    static constexpr int none = -2;
    static constexpr int stop = -1;

    explicit Launcher(SPSCQueue<SeqOut>& out);

    /// Транспорт стоит: активные ячейки и остановки — из снимка проекта.
    void adopt(const SeqData& d);
    /// Дорожка from встала на место to: ход дорожек переезжает с ними.
    void move(int from, int to);
    /// Ожидания и строка сбрасываются.
    void clearQueues();
    void reset();
    /// Старт транспорта (тик 0).
    void start(const SeqData& d);
    /// Тап по ячейке: k — запуск, stop — остановка; повторный тап по ожидающей — отмена.
    void queue(int t, int k);
    /// Тап по строке: с начала следующего такта; atPass — в конце прохода играющей строки.
    void queueScene(int k, bool atPass);
    /// Перед шагом tick: строки и ожидающие клипы. false — транспорт остановился.
    bool beforeStep(const SeqData& d, int tick);
    /// Дорожка играет: активная ячейка с клипом и не остановлена.
    bool plays(const SeqData& d, int t) const { return !stopped[size_t(t)] && d.filled(t, slot[size_t(t)]); }
    bool sceneEnds(const SeqData& d, int tick) const;

    // Аранжировка (ArrangePlay.swift)
    void setFollow(int t, bool on, bool force = false);
    void setFollowAll(bool on, bool force = false);
    void returnToArrange(int t);
    void seek(int pos);
    bool arrangeStep(const SeqData& d, int tick);
    bool anyFollow(const SeqData& d) const;
    void syncArrange(const SeqData& d, int t, int tick);

    std::vector<int> slot;
    std::vector<uint8_t> stopped;
    std::vector<int> queued, trackStart;
    /// Дорожка идёт по таймлайну; ход пересчитать на ближайшем тике (bool[] для ArrAutoPlay).
    std::unique_ptr<bool[]> follow;
    std::vector<uint8_t> arrDirty;
    int arrPos = 0;
    bool arrangeMode = false;
    int arrFrom = 0;
    int sceneRow = -1, sceneStart = 0, sceneLen = 16;
    int sceneQueued = -1;
    bool sceneAtPass = false;
    /// Тик, на котором строки пошли по кругу — там кончаются стемы (−1 — не было).
    int wrappedAt = -1;

private:
    SPSCQueue<SeqOut>& out;
    bool rowFilled(const SeqData& d, int k) const;
    bool sceneDue(int tick) const;
    bool atBoundary(const SeqData& d, int t, int tick) const;
    void apply(int t, int q, int tick);
    void startScene(const SeqData& d, int k, int at);
};

}  // namespace jambo
