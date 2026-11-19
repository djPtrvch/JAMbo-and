// Запись нот с REC — правки шагов дорожки (порт Core/Model/Recording.swift, ветка swift JAMbo).
#include "Track.h"
#include <set>

namespace jambo {

void Track::recordNotes(int index, const std::vector<int>& notes, bool merge, bool overdub, std::optional<double> velocity) {
    const auto it = params.find("level");
    const double level = it == params.end() ? 127 : it->second;
    withStep(index, [&](Step& s) {
        const bool merged = (merge || overdub) && s.on;
        if (!merged) {
            s.lens.reset();
            s.vels.reset();
        }
        if (merged) {
            std::set<int> all(s.notes.begin(), s.notes.end());
            all.insert(notes.begin(), notes.end());
            s.setNotes(std::vector<int>(all.begin(), all.end()));
        } else {
            s.setNotes(notes);
        }
        s.on = true;
        s.setAllLen(1);
        if (velocity) {
            const double lv = std::round(level * *velocity);
            for (int n : notes) {
                const auto f = std::find(s.notes.begin(), s.notes.end(), n);
                if (f == s.notes.end()) continue;
                const int k = int(f - s.notes.begin());
                const double old = merged ? s.noteVel(k).value_or(0) : 0;
                s.setVelLinked(k, std::max(old, lv), level);
            }
        }
    });
}

void Track::recordLength(int index, int len) {
    const int maxLen = length() - index;
    withStep(index, [&](Step& s) {
        if (!s.on) return;
        s.setAllLen(std::max(s.len, std::min(len, maxLen)));
    });
}

bool Track::clearPassStep(int index) {
    const Step* s = step(index);
    if (!s || !(s->on || s->sound || !s->locks.empty())) return false;
    withStep(index, [](Step& x) { x = Step(); });
    return true;
}

}  // namespace jambo
