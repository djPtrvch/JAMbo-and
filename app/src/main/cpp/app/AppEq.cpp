// App: окно EQ (порт части AppModelEq.swift, ветка swift JAMbo): канал — выбранная дорожка, при MIX и MST — мастер;
// правки полос (одна запись UNDO на жест), BYPASS, точки съёма спектров, сдвиги динамических полос. Сравнение дорожек,
// LISTEN, пресеты EQ, коррекция наушников и ввод числом — позже.
#include "App.h"
#include <algorithm>

namespace jambo {

int App::eqChannel() const { return channel_ == Channel::track ? selected_ : EqSpec::masterChannel; }

Eq App::eqTarget() const { return project_.eq(eqChannel()); }

std::optional<EqBand> App::selectedBand() const { return eqTarget().band(eqBand_); }

void App::eqEdit(const std::function<void(Eq&)>& f) {
    const int ch = eqChannel();
    edit([&](Project& p) { p.withEq(ch, f); });
}

void App::toggleEqView() {
    eqView_ = !eqView_;
    if (eqView_) patternView_ = false;
    eqBand_ = -1;
    syncSpectra();
    bump();
}

void App::syncSpectra() {
    if (Spectra* s = audio_->spectra()) {
        s->channel.store(eqView_ ? eqChannel() : -1);
        s->compareTrack.store(-1);
    }
}

void App::selectEqBand(int k) {
    eqBand_ = k;
    bump();
}

void App::addEqBand(double freq, double gain) {
    int k = -1;
    eqEdit([&](Eq& e) { k = e.addBand(freq, gain); });
    if (k >= 0) eqBand_ = k;
    bump();
}

void App::setEqBand(int k, std::optional<double> freq, std::optional<double> gain, std::optional<double> q) {
    eqEdit([&](Eq& e) { e.setBand(k, freq, gain, q); });
}

void App::setEqType(int k, int type) { eqEdit([&](Eq& e) { e.setType(k, type); }); }
void App::setEqSlope(int k, int slope) { eqEdit([&](Eq& e) { e.setSlope(k, slope); }); }
void App::setEqMs(int k, int ms) { eqEdit([&](Eq& e) { e.setMs(k, ms); }); }
void App::setEqDyn(int k, int src) { eqEdit([&](Eq& e) { e.setDyn(k, src); }); }

void App::setEqDynParams(int k, std::optional<double> thr, std::optional<double> range) {
    eqEdit([&](Eq& e) { e.setDynParams(k, thr, range); });
}

void App::toggleEqBand(int k) { eqEdit([&](Eq& e) { e.toggleBand(k); }); }

void App::deleteEqBand(int k) {
    eqEdit([&](Eq& e) { e.deleteBand(k); });
    if (eqBand_ == k) eqBand_ = -1;
    bump();
}

void App::toggleEqBypass() { eqEdit([](Eq& e) { e.on = !e.on; }); }

std::vector<double> App::eqDynOffsets() const {
    const Eq e = eqTarget();
    const int ch = eqChannel();
    std::vector<double> out(size_t(EqSpec::bands), 0);
    for (int k = 0; k < EqSpec::bands; ++k) {
        const auto b = e.band(k);
        if (!e.on || !b || !b->on || !EqSpec::hasGain(b->type) || b->dyn < 0) continue;
        out[size_t(k)] = audio_->eqDynOffset(ch, k);
    }
    return out;
}

std::string App::eqDynName(const EqBand& b) const {
    if (!EqSpec::hasGain(b.type)) return "—";
    if (b.dyn < 0) return "OFF";
    if (b.dyn == 0) return "SELF";
    const int t = b.dyn - 1;
    return t < int(project_.tracks.size()) ? project_.tracks[size_t(t)].name : "T" + std::to_string(b.dyn);
}

void App::eqSpectrum(int n, double dt, std::vector<float>& pre, std::vector<float>& post) {
    const Spectra* s = audio_->spectra();
    if (!s) {
        pre.assign(size_t(n), 1);
        post.assign(size_t(n), 1);
        return;
    }
    spectrum_.frame(*s, n, dt, pre, post);
}

}  // namespace jambo
