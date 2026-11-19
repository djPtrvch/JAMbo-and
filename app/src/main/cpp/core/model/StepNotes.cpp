// Порт Core/Model/StepNotes.swift (ветка swift JAMbo): ноты шага с длиной и velocity — lens и vels параллельны notes.
#include <algorithm>
#include "Clip.h"

namespace jambo {

namespace {
int indexOf(const std::vector<int>& v, int n) {
    auto it = std::find(v.begin(), v.end(), n);
    return it == v.end() ? -1 : int(it - v.begin());
}
}  // namespace

int Step::noteLen(int k) const {
    if (lens && k >= 0 && k < int(lens->size())) return (*lens)[size_t(k)];
    return len;
}

std::optional<double> Step::noteVel(int k) const {
    if (!vels || k < 0 || k >= int(vels->size()) || (*vels)[size_t(k)] < 0) return std::nullopt;
    return (*vels)[size_t(k)];
}

void Step::setAllLen(int n) {
    len = std::max(1, n);
    lens.reset();
}

void Step::setNotes(const std::vector<int>& next) {
    std::vector<int> l;
    std::vector<double> v;
    for (int n : next) {
        const int k = indexOf(notes, n);
        l.push_back(k >= 0 ? noteLen(k) : len);
        v.push_back(k >= 0 ? noteVel(k).value_or(-1) : -1);
    }
    notes = next;
    lens = l;
    vels = v;
    normalize();
}

void Step::addNote(int n, int l, std::optional<double> vel) {
    if (indexOf(notes, n) < 0) {
        std::vector<int> all = notes;
        all.push_back(n);
        std::sort(all.begin(), all.end());
        setNotes(all);
    }
    const int k = indexOf(notes, n);
    if (k < 0) return;
    setNoteLen(k, l);
    setNoteVel(k, vel);
}

void Step::removeNote(int k) {
    if (k < 0 || k >= int(notes.size())) return;
    std::vector<int> all = notes;
    all.erase(all.begin() + k);
    setNotes(all);
    if (notes.empty()) on = false;
}

void Step::setNoteLen(int k, int n) {
    if (k < 0 || k >= int(notes.size())) return;
    std::vector<int> l;
    for (int j = 0; j < int(notes.size()); ++j) l.push_back(noteLen(j));
    l[size_t(k)] = std::max(1, n);
    lens = l;
    normalize();
}

void Step::setNoteVel(int k, std::optional<double> v) {
    if (k < 0 || k >= int(notes.size())) return;
    std::vector<double> all;
    for (int j = 0; j < int(notes.size()); ++j) all.push_back(noteVel(j).value_or(-1));
    all[size_t(k)] = v ? std::max(0.0, std::min(127.0, *v)) : -1;
    vels = all;
    normalize();
}

void Step::setVelLinked(int k, double v, double fallback) {
    if (k < 0 || k >= int(notes.size())) return;
    auto lv = locks.find("level");
    const double old = lv != locks.end() ? lv->second : fallback;
    for (int j = 0; j < int(notes.size()); ++j)
        if (j != k && !noteVel(j)) setNoteVel(j, old);
    setNoteVel(k, v);
    if (vels && !vels->empty()) {
        const double m = *std::max_element(vels->begin(), vels->end());
        if (m >= 0) locks["level"] = m;
    }
    if (soundLocks) {
        std::vector<std::string> sl;
        for (const auto& id : *soundLocks) if (id != "level") sl.push_back(id);
        soundLocks = sl;
    }
}

void Step::normalize() {
    if (lens) {
        const auto& l = *lens;
        if (l.size() != notes.size()) {
            lens.reset();
        } else if (!l.empty()) {
            const int m = *std::max_element(l.begin(), l.end());
            len = m;
            if (std::all_of(l.begin(), l.end(), [m](int x) { return x == m; })) lens.reset();
        } else {
            lens.reset();
        }
    }
    if (vels) {
        const auto& v = *vels;
        if (v.size() != notes.size() || std::all_of(v.begin(), v.end(), [](double x) { return x < 0; })) vels.reset();
    }
}

}  // namespace jambo
