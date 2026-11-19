// Порт Core/Model/RollEdits.swift (ветка swift JAMbo): правки нот из пианоролла — шаг index (p·16 + s) открытого
// клипа, нота — по высоте.
#include <algorithm>
#include "Track.h"
#include "../sequencer/SeqData.h"

namespace jambo {

namespace {
int indexOf(const std::vector<int>& v, int n) {
    auto it = std::find(v.begin(), v.end(), n);
    return it == v.end() ? -1 : int(it - v.begin());
}
}  // namespace

void Track::addRollNote(int index, int pitch, int len, std::optional<double> vel) {
    withStep(index, [&](Step& s) {
        if (!s.on) {
            s.notes.clear();
            s.lens.reset();
            s.vels.reset();
            s.on = true;
        }
        if (int(s.notes.size()) >= SeqData::maxNotes && indexOf(s.notes, pitch) < 0) return;
        s.addNote(pitch, len, vel);
    });
}

void Track::removeRollNote(int index, int pitch) {
    withStep(index, [&](Step& s) {
        const int k = indexOf(s.notes, pitch);
        if (k >= 0) s.removeNote(k);
    });
}

void Track::setRollNoteLen(int index, int pitch, int len) {
    withStep(index, [&](Step& s) {
        const int k = indexOf(s.notes, pitch);
        if (k >= 0) s.setNoteLen(k, len);
    });
}

void Track::setRollNoteVel(int index, int pitch, double vel) {
    auto it = params.find("level");
    const double fallback = it != params.end() ? it->second : 127;
    withStep(index, [&](Step& s) {
        const int k = indexOf(s.notes, pitch);
        if (k >= 0) s.setVelLinked(k, vel, fallback);
    });
}

double Track::rollNoteLevel(const RollNote& n) const {
    if (n.vel) return *n.vel;
    return value("level", step(n.index));
}

std::optional<StepCarry> Track::rollCarry(int index, int pitch) const {
    const Step* s = step(index);
    if (!s || !s->on || s->notes != std::vector<int>{pitch}) return std::nullopt;
    return StepCarry{s->locks, s->sound, s->soundLocks, index};
}

void Track::moveRollNote(int index, int pitch, int dest, int dp, std::optional<StepCarry>& carry) {
    const Step* s = step(index);
    if (!s) return;
    const int k = indexOf(s->notes, pitch);
    if (k < 0) return;
    if (index == dest && pitch == dp) return;
    const int len = s->noteLen(k);
    const std::optional<double> vel = s->noteVel(k);
    removeRollNote(index, pitch);
    const Step* after = step(index);
    if (carry && carry->at == index && index != dest && after && !after->on) {
        withStep(index, [](Step& st) {
            st.locks.clear();
            st.sound.reset();
            st.soundLocks.reset();
        });
        carry->at.reset();
    }
    const Step* ds = step(dest);
    const bool free = ds && !ds->on && ds->locks.empty() && !ds->sound;
    addRollNote(dest, dp, len, vel);
    if (carry && !carry->at && free) {
        const StepCarry c = *carry;
        withStep(dest, [&](Step& st) {
            st.locks = c.locks;
            st.sound = c.sound;
            st.soundLocks = c.soundLocks;
        });
        carry->at = dest;
    }
}

void Track::moveRollNote(int index, int pitch, int dest, int dp) {
    std::optional<StepCarry> none;
    moveRollNote(index, pitch, dest, dp, none);
}

}  // namespace jambo
