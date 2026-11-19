// Снимок паттернов для аудиопотока (порт SeqData, Core/Sequencer/SeqSnapshot.swift, ветка swift JAMbo): плоские
// массивы чисел. У каждого клипа (дорожка·cellsPerTrack + ячейка) свой паттерн: блок шагов (−1 — пусто), длина и
// играющие страницы. Массивы выделены при построении снимка (SeqSnapshot) — колбэк только читает.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace jambo {

struct SeqData {
    static constexpr int maxTracks = 16;
    static constexpr int maxSteps = 8 * 16;
    static constexpr int maxNotes = 8;
    static constexpr int slots = 16;
    /// Ячейки блоков аранжировки со своей копией клипа: после ячеек сетки, по номеру блока на дорожке.
    static constexpr int arrCells = 128;
    static constexpr int cellsPerTrack = slots + arrCells;
    static constexpr int chans = 2;

    int trackCount = 0;
    /// На дорожку: остановлена, активная ячейка (колбэк берёт их, пока транспорт стоит).
    std::vector<uint8_t> stopped;
    std::vector<int32_t> slot;
    /// На клип: блок шагов (−1 — пусто), длина паттерна, страницы from…to, скорость.
    std::vector<int32_t> clipBlock, clipLength, clipFrom, clipTo;
    std::vector<int8_t> clipRate;
    /// На шаг (блок·maxSteps + шаг): триг, длина, ноты (·maxNotes), split, длины и velocity нот, Chance, Ratchet,
    /// p-lock Chance и Ratchet (биты 1 и 2), Swing.
    std::vector<uint8_t> on;
    std::vector<int32_t> len;
    std::vector<uint8_t> noteCount;
    std::vector<int16_t> notes;
    std::vector<uint8_t> split;
    std::vector<int32_t> noteLen;
    std::vector<float> noteVel, prob;
    std::vector<int32_t> ratchet;
    std::vector<uint8_t> probLock;
    std::vector<float> swing;
    /// Свой звук шага (ключ; 0 — звук дорожки) и p-lock голоса, FX, LFO/ENV.
    std::vector<uint64_t> sound;
    std::vector<uint32_t> lockMask;
    std::vector<float> lockVal;
    std::vector<uint32_t> fxMask;
    std::vector<float> fxVal;
    std::vector<uint16_t> modMask;
    std::vector<float> modVal;
    /// Строки: видимых, повторы, строка старта, в конце — стоп, LOOP ROW.
    int rows = 1;
    std::vector<int32_t> repeats;
    int arrStart = 0;
    bool arrEndStop = false;
    bool rowLoop = true;
    /// Последовательности MIX и MST: длина и играющие страницы; p-lock громкостей на шагах MIX.
    std::vector<int32_t> chanLength, chanFrom, chanTo;
    std::vector<uint16_t> mixMask;
    std::vector<float> mixGain;
    /// Аранжировка: блоки дорожки t — arrFirst[t] … + arrCount[t] − 1.
    std::vector<int32_t> arrFirst, arrCount, blkClip, blkStart, blkLen, blkOff;
    int arrEnd = 0, loopFrom = 0, loopTo = 0;
    bool loop = false;
    /// Автоматизация: кривая цели j дорожки t — точки autoFirst[t·count + j] … + autoCount − 1.
    std::vector<int32_t> autoFirst, autoCount;
    std::vector<double> autoX;
    std::vector<float> autoY;
    std::vector<int32_t> autoG;
    std::vector<uint8_t> autoAny;

    /// Значение 0…1 кривой цели j дорожки t на шаге x; −1 — кривой нет или x вне её точек.
    double autoValue(int t, int j, double x) const;
    /// Блок дорожки t на позиции x аранжировки (−1 — нет).
    int block(int t, int x) const;
    bool filled(int t, int k) const {
        return k >= 0 && k < cellsPerTrack && clipBlock[size_t(t * cellsPerTrack + k)] >= 0;
    }
    struct Range {
        int from, to;
    };
    Range clip(int t, int k) const {
        const size_t i = ci(t, k);
        return {clipFrom[i], clipTo[i]};
    }
    int pageLen(int t, int k, int p, int from) const { return pageLen(clipLength[ci(t, k)], p, from); }
    int playLen(int t, int k) const {
        const Range c = clip(t, k);
        return playLen(clipLength[ci(t, k)], c.from, c.to);
    }
    int rate(int t, int k) const { return clipRate[ci(t, k)]; }
    /// Проход клипа в тиках транспорта.
    int tickLen(int t, int k) const { return passTicks(playLen(t, k), rate(t, k)); }
    /// Шаг (p·16 + s) клипа k на позиции n прохода, по кругу.
    int playIndex(int t, int k, int n) const {
        const Range c = clip(t, k);
        return playIndex(clipLength[ci(t, k)], c.from, c.to, n);
    }
    /// Шаг последовательности MIX или MST на позиции n от PLAY, по кругу.
    int chanIndex(int c, int n) const { return playIndex(chanLength[size_t(c)], chanFrom[size_t(c)], chanTo[size_t(c)], n); }
    /// Индекс шага pos клипа k в массивах шагов; −1 — ячейка пуста.
    int stepIndex(int t, int k, int pos) const {
        const int b = clipBlock[ci(t, k)];
        if (b < 0 || pos < 0 || pos >= maxSteps) return -1;
        return b * maxSteps + pos;
    }
    /// Для дорожки с активным клипом из проекта (тесты, интерфейс).
    int playLen(int t) const { return playLen(t, slot[size_t(t)]); }
    int playIndexActive(int t, int n) const { return playIndex(t, slot[size_t(t)], n); }

    static int pageLen(int length, int p, int from) {
        const int n = 16;
        if (length > from * n) return std::max(0, std::min(n, length - p * n));
        return n;
    }
    static int playLen(int length, int from, int to) {
        int n = 0;
        for (int p = from; p <= to; ++p) n += pageLen(length, p, from);
        return std::max(1, n);
    }
    static int playIndex(int length, int from, int to, int n) {
        const int total = playLen(length, from, to);
        int r = ((n % total) + total) % total;
        for (int p = from; p <= to; ++p) {
            const int l = pageLen(length, p, from);
            if (r < l) return p * 16 + r;
            r -= l;
        }
        return from * 16;
    }
    /// Проход в тиках (шестнадцатых) с учётом скорости rate (степень двойки).
    static int passTicks(int len, int rate) {
        if (rate > 0) return std::max(1, (len + 1) / 2);
        return len << (-rate);
    }

private:
    size_t ci(int t, int k) const { return size_t(t * cellsPerTrack + std::max(0, std::min(cellsPerTrack - 1, k))); }
};

}  // namespace jambo
