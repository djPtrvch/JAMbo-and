// Голос своих движков JAMbo — KICK, ANALOG, FM, WTABLE, SUB. Порт 1:1 Core/Jambo/JamboVoice.swift и
// JamboAnalog.swift (ветка swift JAMbo); поведение — docs/ai/synth-engines.md JAMbo.
// Совпадает с эталонами CoreTests/golden/j16–j19 по отсчётам. Состояние и арифметика — Double.
#pragma once
#include "Jambo.h"
#include "JamboTables.h"
#include "Wavetable.h"

namespace jambo {

class JamboVoice {
public:
    JamboVoice(const JamboTables& tables, double sampleRate);

    /// Ручки (mod): p1–p4 — 0…1, decay — мс.
    void set(double note, double q1, double q2, double q3, double q4, double decay) {
        note_ = note; t1_ = q1; t2_ = q2; t3_ = q3; t4_ = q4; td_ = decay;
    }
    /// ANALOG: режим модуляции (0…4: нет, SYNC, FM, RING, AM) и октава B; смена на звучащей ноте — кроссфейд 5 мс.
    void setAnalog(int mode, bool oct);
    /// Нота в кадре now (абсолютный кадр), tau — время дорожки с дрейфом на этот кадр.
    void trigger(int engine, double note, double q1, double q2, double q3, double q4, double decay,
                 double now, double tau);
    /// Кусок n кадров (n ≤ 24) в out; driftRatio — множитель дрейфа дорожки (ANALOG).
    void render(float* out, int n, double driftRatio, WtRef table = WtRef());

    int engine() const { return engine_; }
    /// Последний отсчёт DRUM до клиппера (сглаживание стыка при ретриге); 0 — голос молчал.
    double last = 0;

private:
    double rand();
    void drum(float* out, int n);
    void analog(float* out, int n, double ratio);
    void subBass(float* out, int n);
    void fm(float* out, int n);
    void wtable(float* out, int n, const WtRef& tb);

    const JamboTables& tables_;
    double sr_;
    int engine_ = J::drum;
    double note_ = 48;
    double t1_ = 0.5, t2_ = 0.5, t3_ = 0.5, t4_ = 0.5, td_ = 500;
    double s1_ = 0.5, s2_ = 0.5, s3_ = 0.5, s4_ = 0.5, sd_ = 500;
    double s3From_ = 0.5;   // ручка 3 в начале куска (KICK: DRIVE плавно по отсчётам)
    double t_ = 0;
    int64_t rnd_ = 22222;
    // DRUM
    double p1_ = 0, dc_ = 0;
    // ANALOG: осцилляторы A и B, у каждого своя форма (смена — кроссфейд).
    double a1_ = 0, a2_ = 0, tri1_ = 0, tri2_ = 0;
    int wCur_ = 0, wPrev_ = 0;
    double wMix_ = 1;
    int bCur_ = 0, bPrev_ = 0;
    double bMix_ = 1;
    // ANALOG: модуляция B осциллятором A и октава B.
    int mCur_ = 0, mPrev_ = 0;
    double mMix_ = 1;
    bool aOct_ = false;
    double o3_ = 0, pB_ = 0, sAfter_ = 0;
    // SUB: фильтр постоянной составляющей после GRIT.
    double hx_ = 0, hy_ = 0;
    // FM
    double pc_ = 0, pm_ = 0, m1_ = 0, m2_ = 0;
    double hb_[J::hbLen * 2] = {};
    int hbPos_ = 0;
    // WTABLE: центр и до четырёх боковых голосов.
    double w0_ = 0, wl_ = 0, wr_ = 0, wl2_ = 0, wr2_ = 0;
    // Вход новой ноты (кадров с её начала).
    int fade_ = J::fadeIn;
};

}  // namespace jambo
