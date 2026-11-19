// Порт части Core/Model/Transition.swift (ветка swift JAMbo): звук дорожки. Переход MIX — позже.
#include "Track.h"

namespace jambo {

TrackSound Track::sound() const {
    TrackSound s;
    s.sample = sample;
    s.params = params;
    s.folder = folder;
    s.slices = slices;
    if (!warps.empty()) s.warps = warps;
    s.table = table;
    if (!grain.empty()) s.grain = grain;
    s.assigns = assigns;
    s.eq = eq;
    return s;
}

void Track::setSound(const TrackSound& s) {
    sample = s.sample;
    params = s.params;
    folder = s.folder;
    slices = s.slices;
    warps = s.warps.value_or(std::map<std::string, WarpInfo>());
    table = s.table;
    grain = s.grain.value_or(std::string());
    assigns = s.assigns;
    eq = s.eq;
}

}  // namespace jambo
