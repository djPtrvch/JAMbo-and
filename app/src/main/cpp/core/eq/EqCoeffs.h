// Коэффициенты биквада полосы EQ (порт Core/EQ/EqCoeffs.swift, ветка swift JAMbo): формулы RBJ — peaking, lowshelf и
// highshelf с S = 1, highpass и lowpass с Q в dB, notch, bandpass (фильтр детектора Dynamic); частота — доля Найквиста.
// АЧХ полосы — для кривой EQ (каскад — сумма ступеней).
#pragma once
#include <vector>
#include "EqModel.h"

namespace jambo {

namespace EqCoeffs {
/// Тип фильтра детектора «полосовой» (EqDetectors::bandpass).
constexpr int bandpass = 6;
/// Коэффициенты (b0, b1, b2, a1, a2; нормированы на a0) в out.
void coeffs(int type, double hz, double db, double q, double sr, double* out);
/// АЧХ полосы на частотах hz, dB.
std::vector<double> response(const EqBand& b, const std::vector<double>& hz, double sampleRate = 48000);
double response(const EqBand& b, double hz, double sampleRate = 48000);
}  // namespace EqCoeffs

}  // namespace jambo
