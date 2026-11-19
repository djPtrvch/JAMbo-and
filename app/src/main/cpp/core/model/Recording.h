// Шаг записи по времени (порт enum RecordClock, Core/Model/Recording.swift, ветка swift JAMbo). Правки шагов при
// записи — Track::recordNotes, recordLength, clearPassStep (Recording.cpp).
#pragma once
#include <algorithm>
#include <cmath>

namespace jambo {
namespace RecordClock {
/// k = tick + (кадр − кадр следующего шага) / длина шага, с округлением (QUANTIZE) или вниз — на шаг, в котором нажали.
inline int tick(double frame, int nextTick, double nextFrame, double stepFrames, bool quantize) {
    const double d = (frame - nextFrame) / stepFrames;
    const double k = quantize ? std::round(d) : std::floor(d);
    return std::max(0, nextTick + int(k));
}
/// Длина ноты в шагах по времени удержания.
inline int length(double seconds, double stepSeconds) { return std::max(1, int(std::round(seconds / stepSeconds))); }
}  // namespace RecordClock
}  // namespace jambo
