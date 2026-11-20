// Ввод числом в полях окон модулей мастера (порт Core/Model/MasterInput.swift, ветка swift JAMbo): текст для окна
// ввода и разбор введённого — «auto» (RELEASE), «off» (BASS, FOCUS, SC HPF), «1.2 s» у времени, «1.5k» у частоты, имя
// пункта у TYPE; значение — в пределах параметра, с его шагом.
#pragma once
#include <optional>
#include <string>
#include "Params.h"

namespace jambo {
namespace MasterInput {
bool isTime(const ParamDef& p);
std::optional<double> offValue(const ParamDef& p);
std::string text(const ParamDef& p, double v);
std::optional<double> parse(const ParamDef& p, const std::string& text);
double clamp(const ParamDef& p, double v);
}  // namespace MasterInput
}  // namespace jambo
