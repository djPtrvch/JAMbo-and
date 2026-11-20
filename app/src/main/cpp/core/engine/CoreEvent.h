// Сообщение от интерфейса в аудиоколбэк (порт enum CoreEvent, Core/Engine/CoreEvent.swift, ветка swift JAMbo): простая
// запись с видом и полями — копируется в очередь SPSCQueue без выделений.
#pragma once
#include <cstdint>
#include "sampler/NoteLocks.h"

namespace jambo {

struct CoreEvent {
    enum class Kind : uint8_t {
        none,
        /// Тест-тон: включить (on) / частота (f).
        tone,
        toneFrequency,
        /// PLAY (on) — старт с начала следующего блока или стоп.
        play,
        /// PLAY по MIDI-клоку: шаг 0 — в кадре frame.
        playAt,
        /// Шаг a по MIDI-клоку должен звучать в кадре x.
        clockPhase,
        /// Темп x (BPM).
        tempo,
        metronome,
        /// Громкость метронома f (0…2).
        metronomeGain,
        /// Тап по ячейке во время игры: дорожка a, ячейка b (или Launcher::stop).
        launch,
        /// Тап по строке a во время игры; on — в конце прохода строки.
        scene,
        adoptPlay,
        /// Аранжировщик: PLAY в окне аранжировщика (on) с позиции a.
        arrange,
        /// Возврат дорожки a к таймлайну (a < 0 — всех).
        returnToArrange,
        leaveArrange,
        /// Курсор аранжировки на позицию a.
        seekArrange,
        /// Ручка цели b дорожки a под пальцем (on) — кривая не действует.
        autoHold,
        /// Дорожку переставили во время игры: a → b.
        moveTrack,
        /// Живая нота: дорожка a, нота b, касание c, сила f; отпускание: дорожка a, касание c.
        noteOn,
        noteOff,
        /// Арпеджиатор дорожки a: on, направление b, скорость c, октавы d, длина ноты f, LATCH latch.
        arp,
        /// ▶ редактора: сэмпл hash, отрезок x…y; стоп.
        preview,
        previewStop,
        /// ▶ WARP: дорожка a, сэмпл hash.
        warpPreview,
        /// ▶ пресета: дорожка a, нота b, звук hash, p-lock locks.
        presetNote,
        /// Запись стемов: готовность канала a (on); дописать канал a; PLAY во время записи.
        stemArm,
        stemFinish,
        stopAfterStems,
        /// REC редактора сэмпла: источник a; стоп.
        captureStart,
        captureStop,
    };
    Kind kind = Kind::none;
    int a = 0, b = 0, c = 0, d = 0;
    bool on = false, latch = false;
    float f = 0;
    double x = 0, y = 0;
    int64_t frame = 0;
    uint64_t hash = 0;
    NoteLocks locks;

    static CoreEvent make(Kind k) {
        CoreEvent e;
        e.kind = k;
        return e;
    }
    static CoreEvent tone(bool on) { auto e = make(Kind::tone); e.on = on; return e; }
    static CoreEvent toneFrequency(float hz) { auto e = make(Kind::toneFrequency); e.f = hz; return e; }
    static CoreEvent play(bool on) { auto e = make(Kind::play); e.on = on; return e; }
    static CoreEvent playAt(int64_t frame) { auto e = make(Kind::playAt); e.frame = frame; return e; }
    static CoreEvent clockPhase(int step, double frame) { auto e = make(Kind::clockPhase); e.a = step; e.x = frame; return e; }
    static CoreEvent tempo(double bpm) { auto e = make(Kind::tempo); e.x = bpm; return e; }
    static CoreEvent metronome(bool on) { auto e = make(Kind::metronome); e.on = on; return e; }
    static CoreEvent metronomeGain(float g) { auto e = make(Kind::metronomeGain); e.f = g; return e; }
    static CoreEvent launch(int t, int slot) { auto e = make(Kind::launch); e.a = t; e.b = slot; return e; }
    /// Правка строк при игре: ход дорожек — по новому проекту.
    static CoreEvent adoptPlay() { return make(Kind::adoptPlay); }
    static CoreEvent scene(int row, bool atPass) { auto e = make(Kind::scene); e.a = row; e.on = atPass; return e; }
    static CoreEvent arrange(bool on, int from) { auto e = make(Kind::arrange); e.on = on; e.a = from; return e; }
    static CoreEvent returnToArrange(int t) { auto e = make(Kind::returnToArrange); e.a = t; return e; }
    static CoreEvent seekArrange(int pos) { auto e = make(Kind::seekArrange); e.a = pos; return e; }
    static CoreEvent autoHold(int t, int target, bool on) {
        auto e = make(Kind::autoHold); e.a = t; e.b = target; e.on = on; return e;
    }
    static CoreEvent moveTrack(int from, int to) { auto e = make(Kind::moveTrack); e.a = from; e.b = to; return e; }
    static CoreEvent noteOn(int t, int note, int id, float velocity) {
        auto e = make(Kind::noteOn); e.a = t; e.b = note; e.c = id; e.f = velocity; return e;
    }
    static CoreEvent noteOff(int t, int id) { auto e = make(Kind::noteOff); e.a = t; e.c = id; return e; }
    static CoreEvent arp(int t, bool on, int dir, int rate, int octaves, float gate, bool latch) {
        auto e = make(Kind::arp); e.a = t; e.on = on; e.b = dir; e.c = rate; e.d = octaves; e.f = gate; e.latch = latch;
        return e;
    }
    static CoreEvent preview(uint64_t hash, double a, double b) {
        auto e = make(Kind::preview); e.hash = hash; e.x = a; e.y = b; return e;
    }
    static CoreEvent warpPreview(int t, uint64_t hash) { auto e = make(Kind::warpPreview); e.a = t; e.hash = hash; return e; }
    static CoreEvent presetNote(int t, int note, uint64_t hash, const NoteLocks& locks) {
        auto e = make(Kind::presetNote); e.a = t; e.b = note; e.hash = hash; e.locks = locks; return e;
    }
    static CoreEvent stemArm(int ch, bool on) { auto e = make(Kind::stemArm); e.a = ch; e.on = on; return e; }
    static CoreEvent stemFinish(int ch) { auto e = make(Kind::stemFinish); e.a = ch; return e; }
    static CoreEvent captureStart(int source) { auto e = make(Kind::captureStart); e.a = source; return e; }
};

}  // namespace jambo
