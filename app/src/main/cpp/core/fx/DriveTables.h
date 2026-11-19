// Таблицы Drive и 4× оверсэмплера (порт 1:1 Core/FX/DriveTables.swift, ветка swift JAMbo): ядро FIR (окно Кайзера)
// и его фазы, антипроизводные tanh и FUZZ (Симпсон, шаг 1/32 до 12), компенсация громкости по типу и DRIVE
// (шаг 0,1 dB). Строятся один раз (shared — при создании первого Drive, вне колбэка).
#pragma once
#include <functional>
#include <vector>

namespace jambo {

class DriveTables {
public:
    // Оверсэмплер.
    static constexpr int oversample = 4;
    static constexpr int firLen = 129;
    static constexpr double kaiserBeta = 9;
    static constexpr int phaseLen = 33;
    static constexpr int ph0Delay = 16;
    static constexpr int decC = 64;
    // Drive (DRIVE_CONST).
    static constexpr double driveDb = 36;
    static constexpr double tubeBias = 0.35;
    static constexpr double refPeak = 0.25;
    static constexpr int foldHarm = 12;
    static constexpr double foldTrimDb = 0.4;
    static constexpr double compStepDb = 0.1;
    static constexpr double tableMax = 12;
    static constexpr double tableStep = 1.0 / 32;
    static constexpr int tableN = 384;   // 12 / (1/32)
    static constexpr int compN = 360;    // 36 / 0,1

    static const DriveTables& shared();
    DriveTables();

    /// Фазы 1–3 интерполятора (по 33 отвода, усиление 4); фаза 0 — задержка на 16 с усилением ph0Gain.
    std::vector<double> phases;
    double ph0Gain;
    /// Децимация: половина симметричного ядра (64) и центр.
    std::vector<double> decHalf;
    double decMid;
    /// Антипроизводные (F) и значения (D) tanh и FUZZ на [0, 12] (tableN + 2).
    std::vector<double> tanhF, tanhD, fuzzF, fuzzD;
    double tanhMax, fuzzMax;
    /// tanh(bias), 1 − tanh², F(bias) — для TUBE.
    double tb, td, lcB;
    /// Компенсация громкости: 5 типов × (compN + 2).
    std::vector<double> comp;

    static double fuzzY(double u);
    /// F(|u|) по таблице (кубический Эрмит); за 12 кривая ≈ 1 — F растёт линейно.
    static double tableF(const double* F, const double* D, double fmax, double u);

    /// Антипроизводная кривой y (Симпсон) и её значения на [0, 12] — F и D по tableN + 2.
    static void makeTable(double* F, double* D, const std::function<double(double)>& y);

private:
    static double besselI0(double x);
    static void bandLimit(std::vector<double>& v, int H);
    static void makeComp(double* t, int type, const std::function<double(double)>& f);
};

}  // namespace jambo
