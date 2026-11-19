// Порт части Core/Model/Transition.swift (ветка swift JAMbo): звук дорожки. Переход MIX — позже.
#include "Project.h"

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

void Track::settleSound() {
    const std::optional<TrackSound> src = clipFilled(slot) ? clips[size_t(slot)]->src : std::nullopt;
    if (src && !home) {
        home = sound();
        setSound(*src);
    } else if (!src && home) {
        const TrackSound own = *home;
        setSound(own);
        home.reset();
    }
}

double Track::volSlot(int k) const {
    if (clipFilled(k) && clips[size_t(k)]->src && clips[size_t(k)]->src->vol) return *clips[size_t(k)]->src->vol;
    return vol;
}

TrackSound Track::soundSlot(int k) const {
    if (k == slot) return sound();
    if (clipFilled(k) && clips[size_t(k)]->src) return *clips[size_t(k)]->src;
    return home ? *home : sound();
}

std::pair<int, int> Project::clipKey(int t, int k) const {
    if (t < 0 || t >= int(tracks.size()) || !tracks[size_t(t)].clipFilled(k)) return {scale, root};
    const Clip& c = *tracks[size_t(t)].clips[size_t(k)];
    return {c.scale.value_or(scale), c.root.value_or(root)};
}

}  // namespace jambo
