// Сообщения из колбэка в интерфейс (порт enum SeqOut, Core/Sequencer/Sequencer.swift, ветка swift JAMbo): простая
// запись с видом и полями — копируется в очередь без выделений.
#pragma once
#include <cstdint>

namespace jambo {

struct SeqOut {
    enum class Kind : uint8_t {
        none,
        /// Шаг pos дорожки track начал играть (−1 — стоит); track maxTracks + c — MIX (0) или MST (1).
        step,
        /// Сработала нота дорожки (каждый удар рэтчета) в кадре frame.
        trig,
        /// Шаг pos прошёл Chance (один раз на шаг).
        fired,
        /// Доля метронома (on — акцент).
        beat,
        /// Дорожка играет клип slot (или остановлена, on) с тика start.
        clip,
        /// Ожидание у дорожки: value — Launcher::none, Launcher::stop или ячейка.
        queue,
        /// Играет строка row (−1 — нет) с тика start, проход len шагов.
        scene,
        /// Ожидающая строка (−1 — нет).
        sceneQueue,
        /// Транспорт остановился сам.
        ended,
        /// Курсор аранжировки на текущем шаге.
        arrange,
        /// Дорожка идёт по таймлайну (on) или играет сетку.
        follow,
    };
    Kind kind = Kind::none;
    int track = 0;
    /// pos, slot, value, row или позиция аранжировки.
    int a = 0;
    /// start (тик) или len.
    int b = 0;
    int c = 0;
    bool on = false;
    int64_t frame = 0;

    static SeqOut step(int t, int pos) { return {Kind::step, t, pos, 0, 0, false, 0}; }
    static SeqOut trig(int t, int64_t f) { return {Kind::trig, t, 0, 0, 0, false, f}; }
    static SeqOut fired(int t, int pos) { return {Kind::fired, t, pos, 0, 0, false, 0}; }
    static SeqOut beat(bool accent) { return {Kind::beat, 0, 0, 0, 0, accent, 0}; }
    static SeqOut clip(int t, int slot, bool stopped, int start) { return {Kind::clip, t, slot, start, 0, stopped, 0}; }
    static SeqOut queue(int t, int value) { return {Kind::queue, t, value, 0, 0, false, 0}; }
    static SeqOut scene(int row, int start, int len) { return {Kind::scene, 0, row, start, len, false, 0}; }
    static SeqOut sceneQueue(int row) { return {Kind::sceneQueue, 0, row, 0, 0, false, 0}; }
    static SeqOut ended() { return {Kind::ended, 0, 0, 0, 0, false, 0}; }
    static SeqOut arrange(int pos) { return {Kind::arrange, 0, pos, 0, 0, false, 0}; }
    static SeqOut follow(int t, bool on) { return {Kind::follow, t, 0, 0, 0, on, 0}; }
};

}  // namespace jambo
