// Порт части Core/Model/Clips.swift (ветка swift JAMbo): ячейки и видимые строки сетки паттернов. Остальное — позже.
#include "Project.h"

namespace jambo {

void Track::putEmptyClip(int k, std::optional<int> scale, std::optional<int> root) {
    Clip c;
    c.scale = scale;
    c.root = root;
    clips[size_t(k)] = c;
}

bool Project::rowFilled(int k) const {
    for (const auto& t : tracks) if (t.clipFilled(k)) return true;
    return false;
}

int Project::visibleRows() const {
    const int lim = rowLimit();
    int n = std::max(1, rows);
    for (int k = 0; k < lim; ++k) if (rowFilled(k)) n = std::max(n, k + 1);
    return std::min(n, lim);
}

void Project::fillRows() {
    const int n = visibleRows();
    for (auto& t : tracks)
        for (int k = 0; k < n; ++k)
            if (!t.clipFilled(k)) t.putEmptyClip(k, scale, root);
}

}  // namespace jambo
