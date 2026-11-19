// Снимок паттернов (порт SeqSnapshot, Core/Sequencer/SeqSnapshot.swift, ветка swift JAMbo): строится в основном потоке
// после каждой правки, передаётся в колбэк; прежний освобождается в основном потоке.
#pragma once
#include "SeqData.h"

namespace jambo {

struct Project;

class SeqSnapshot {
public:
    explicit SeqSnapshot(const Project& project);
    SeqSnapshot(const SeqSnapshot&) = delete;
    SeqSnapshot& operator=(const SeqSnapshot&) = delete;
    ~SeqSnapshot() { liveCount -= 1; }

    const SeqData& data() const { return data_; }
    /// Сколько снимков сейчас в памяти — для проверки утечек.
    static int liveCount;
    /// Снимок тот же: шаги, клипы, строки и ручки Chance, Ratchet, Swing не менялись.
    static bool same(const Project& a, const Project& b);

private:
    SeqData data_;
};

}  // namespace jambo
