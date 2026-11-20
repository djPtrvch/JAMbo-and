// Перестановка дорожки во время игры (отличие Android: в Swift-версии голоса сдвинутых дорожек гаснут, и звук
// замолкал до следующего шага): блоки голосов дорожек сдвигаются вместе с дорожками, на месте — std::rotate, без
// выделения памяти (колбэк). Дорожка i встала на место j (i < j — сдвиг влево остальных) или наоборот.
#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace jambo {

struct TrackMove {
    int from = -1, to = -1;
    bool valid() const { return from >= 0 && to >= 0 && from != to; }

    /// Старые и новые id дорожек (count штук): одна дорожка переехала — её ход; иначе — пусто.
    static TrackMove find(const int* oldIds, const int* newIds, int count) {
        int i = 0, j = count - 1;
        while (i < count && oldIds[i] == newIds[i]) i++;
        while (j > i && oldIds[j] == newIds[j]) j--;
        if (i >= j) return {};
        // from = i → to = j: новые [i, j) = старые [i + 1, j], новое j = старое i.
        bool left = newIds[j] == oldIds[i];
        for (int k = i; left && k < j; k++) left = newIds[k] == oldIds[k + 1];
        if (left) return {i, j};
        // from = j → to = i: новое i = старое j, новые (i, j] = старые [i, j).
        bool right = newIds[i] == oldIds[j];
        for (int k = i + 1; right && k <= j; k++) right = newIds[k] == oldIds[k - 1];
        if (right) return {j, i};
        return {};
    }

    /// Блоки дорожек вектора (block — элементов на дорожку) — как переезд дорожки from на место to.
    template <class V>
    void apply(V& v, size_t block) const {
        if (!valid() || block == 0) return;
        const int a = std::min(from, to), b = std::max(from, to);
        if (size_t(b + 1) * block > v.size()) return;
        auto first = v.begin() + long(size_t(a) * block);
        auto last = v.begin() + long(size_t(b + 1) * block);
        if (from < to) std::rotate(first, first + long(block), last);
        else std::rotate(first, last - long(block), last);
    }
    template <class V>
    void applyPerTrack(V& v, int tracks) const {
        if (tracks > 0 && v.size() % size_t(tracks) == 0) apply(v, v.size() / size_t(tracks));
    }
};

}  // namespace jambo
