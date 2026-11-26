// Мастер (порт MasterButtons.swift и MasterMeters из MasterIndicator.swift, ветка swift JAMbo): индикация COMP | CLIP |
// LIM при MST, кнопки модулей — с SHIFT BYPASS. Окна модулей (тап без SHIFT) — позже.
#include "App.h"

namespace jambo {

void App::tickMasterMeters(double now) {
    MasterChain* m = audio_->master();
    if (!m) return;
    // Спектры сатуратора пишутся, только пока его окно на экране.
    if (masterWin() != 0 && m->satOn.load() != 0) m->satOn.store(0);
    // Скоп — только пока виден (MST и MIX; при MIX — без осциллоскопа).
    MasterScope& sc = m->scope;
    if (channel_ != Channel::track) {
        const std::array<bool, 4> on = channel_ == Channel::master ? scopeOn_ : std::array<bool, 4>{};
        long mask = MasterScope::gonioBit;
        for (int k = 0; k < 4; k++)
            if (on[size_t(k)]) mask |= 1L << k;
        sc.mask.store(mask);
        scope_.tick(sc, on);
        if (channel_ == Channel::mix) bandStereo_.tick(sc);
    } else if (sc.mask.load() != 0) {
        sc.mask.store(0);
    }
    tickMixAnalysis();
    // LISTEN сбрасывается, когда EQ закрыли (любым способом) или он показывает другой канал — без случайного
    // приглушения звука.
    if (eqListenHeld_ && (!eqView_ || eqChannel() != eqListenChannel_)) stopEqListen();
    tickMakeup();
    if (Levels* lv = audio_->levels()) loudHist_.tick(lv->mixLoudness, meters_.framePeak(Levels::master), playing_, now);
    if (channel_ != Channel::master) return;
    masterMeters_.comp.tick(m->compTele, project_.cmpBypass, now);
    masterMeters_.clip.tick(m->clipTele, project_.clipBypass, now);
    masterMeters_.lim.tick(m->limTele, project_.limBypass, now);
}

bool App::masterBypass(int m) const {
    switch (m) {
    case 0: return project_.satBypass;
    case 1: return project_.cmpBypass;
    case 2: return project_.clipBypass;
    default: return project_.limBypass;
    }
}

void App::toggleMasterBypass(int m) {
    edit([&](Project& p) {
        switch (m) {
        case 0: p.satBypass = !p.satBypass; break;
        case 1: p.cmpBypass = !p.cmpBypass; break;
        case 2: p.clipBypass = !p.clipBypass; break;
        default: p.limBypass = !p.limBypass; break;
        }
    });
}

void App::masterButton(int m) {
    if (m < 0 || m > 3 || channel_ != Channel::master) return;
    if (shiftHeld_) toggleMasterBypass(m);
    else openMasterWindow(m);
}

void App::resetLoudHistory() {
    loudHist_.reset();
    if (Levels* lv = audio_->levels()) lv->mixLoudness.resetRequests.store(lv->mixLoudness.resetRequests.load() + 1);
    if (channel_ != Channel::track) resetLoudness();
}

void App::setMixMode(int m) {
    if (m < 0 || m > 3 || m == mixMode_) return;
    mixMode_ = m;
    bump();
}

/// FREQ, MASK и LOUD — только пока видны (MIX, не PAN).
void App::tickMixAnalysis() {
    Levels* lv = audio_->levels();
    if (!lv) return;
    TrackTaps& taps = lv->trackTaps;
    if (channel_ == Channel::mix && mixMode_ != 1) {
        if (taps.on.load() == 0) taps.on.store(1);
        const int n = std::min(TrackTaps::tracks, int(project_.tracks.size()));
        std::vector<bool> aud;
        for (int t = 0; t < n; t++) aud.push_back(project_.audible(t));
        mixAn_.tick(taps, n, aud, mixMode_ == 3);
    } else if (taps.on.load() != 0) {
        taps.on.store(0);
        mixAn_.reset();
    }
}

void App::toggleScope(int k) {
    if (k < 1 || k > 3) return;
    scopeOn_[size_t(k)] = !scopeOn_[size_t(k)];
    bump();
}

double App::masterValue(const std::string& id, double def) const {
    const auto it = project_.master.find(id);
    return it == project_.master.end() ? def : it->second;
}

}  // namespace jambo
