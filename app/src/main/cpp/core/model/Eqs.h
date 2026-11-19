// Равенство полей структур модели: tie() у каждой — сравнение одной строкой (Swift Equatable).
#pragma once
#include <tuple>

#define JAMBO_EQ(T)                                                                      \
    bool operator==(const T& o) const { return tie() == o.tie(); }                      \
    bool operator!=(const T& o) const { return !(*this == o); }
