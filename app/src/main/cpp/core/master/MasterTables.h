// Таблицы мастера (порт Core/Master/MasterTables.swift, ветка swift JAMbo): оверсэмплер 4× и антипроизводная tanh — общие
// с Drive (DriveTables); здесь — антипроизводная EDGE, компенсация громкости сатуратора (3 типа × шаг 0,1 dB до 24 dB) и
// FIR true peak лимитера (окно Блэкмана, фазы Float32). Строятся один раз (shared, вне колбэка).
#pragma once
#include <cmath>
#include <functional>
#include <vector>
#include "fx/DriveTables.h"

namespace jambo {

class MasterTables {
public:
    static constexpr double tubeBias = 0.35;
    static constexpr double refPeak = 0.25;
    static constexpr double maxDriveDb = 24;
    static constexpr double compStepDb = 0.1;
    static constexpr int compN = 240;
    static constexpr int limTaps = 12;
    static constexpr int limFirDelay = 6;

    static const MasterTables& shared();
    explicit MasterTables(const DriveTables& drive = DriveTables::shared());

    const DriveTables& d;
    std::vector<double> edgeF, edgeD;
    double edgeMax;
    /// Компенсация сатуратора: 3 типа × (compN + 2).
    std::vector<double> comp;
    /// Фазы true peak лимитера: 4 × 12.
    std::vector<double> limPhases;

    static double edgeY(double u) { return u / std::sqrt(std::sqrt(1 + u * u * u * u)); }
    /// Кривая окна сатуратора (TRANSFER), делённая на компенсацию громкости.
    double satCurve(int type, double driveDb, double x) const;

private:
    static void makeComp(double* t, const std::function<double(double)>& f);
    static void makeLimFir(double* out);
};

}  // namespace jambo
