// Команды TO ARRANGE и перетаскивание выделения из PATTERNS.
#include "App.h"

namespace jambo {

void App::sendSelectionToArrange(const GridSelection& selection, int step, int mode) {
    const int grid = project_.arrange.snap ? project_.arrange.gridSteps() : 1;
    const int at = mode == 2 ? (project_.arrangeEnd() + grid - 1) / grid * grid
        : project_.arrange.snapped(step < 0 ? arrangeCursor_ : std::min(step, 1'000'000));
    if (mode == 1) {
        Project probe = project_;
        const auto end = probe.selectionToArrange(selection, at, arrangeRepeats_);
        if (!end) return;
        edit([&](Project& p) {
            p.insertArrangeSpace(at, *end - at);
            p.selectionToArrange(selection, at, arrangeRepeats_);
        });
    } else {
        edit([&](Project& p) { p.selectionToArrange(selection, at, arrangeRepeats_); });
    }
}

std::vector<int> App::previewGridSelection(const GridSelection& selection, int step) const {
    Project probe = project_;
    const int at = project_.arrange.snapped(std::clamp(step, 0, 1'000'000));
    const auto end = probe.selectionToArrange(selection, at, arrangeRepeats_);
    std::vector<int> result{0};
    if (!end) return result;
    for (size_t t = 0; t < project_.tracks.size(); ++t) {
        const auto& old = project_.tracks[t].blocks;
        const auto& next = probe.tracks[t].blocks;
        if (old != next && std::any_of(old.begin(), old.end(), [&](const ArrBlock& block) {
            return block.start < *end && block.end() > at;
        })) result[0] = 1;
        for (const auto& block : next) {
            if (block.start >= at && std::find(old.begin(), old.end(), block) == old.end())
                result.insert(result.end(), {int(t), block.start, block.length});
        }
    }
    return result;
}

}  // namespace jambo
