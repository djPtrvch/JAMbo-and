// Перенос выделенных строк и клипов на таймлайн (Core/Model/GridSelection.swift).
#include "Project.h"

namespace jambo {

std::optional<int> Project::selectionToArrange(const GridSelection& selection, std::optional<int> start, bool repeats) {
    auto rows = selection.rows;
    size_t count = 0;
    for (const auto& [row, tracks] : selection.cells) {
        rows.insert(row);
        count += tracks.size();
    }
    const bool many = rows.size() > 1 || count > 1;
    std::optional<int> last;
    for (int row : rows) {
        if (row < 0 || row >= visibleRows() || row >= int(scenes.size())) continue;
        std::optional<int> end;
        if (selection.rows.count(row)) {
            end = rowToArrange(row, start, repeats);
        } else {
            const auto& tracks = selection.cells.at(row);
            const auto& scene = scenes[size_t(row)];
            const auto span = placeRow(row, {tracks.begin(), tracks.end()},
                repeats ? std::max(1, scene.repeats) : 1, start);
            if (span) {
                end = span->second;
                if (many && !scene.name.empty()) {
                    auto& markers = arrange.markers;
                    markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const ArrMarker& m) {
                        return m.at >= span->first && m.at < span->second;
                    }), markers.end());
                    addMarker(span->first, scene.name, span->second);
                }
            }
        }
        if (end) start = last = end;
    }
    return last;
}

void Project::insertArrangeSpace(int start, int length) {
    if (length <= 0) return;
    const int at = std::max(0, start);
    for (auto& track : tracks) track.shiftArrange(at, length);
    for (auto& marker : arrange.markers) {
        if (marker.at >= at) {
            marker.at += length;
            if (marker.end) *marker.end += length;
        } else if (marker.end && *marker.end > at) {
            *marker.end += length;
        }
    }
}

}  // namespace jambo
