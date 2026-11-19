#include "link_session.h"
#include <ableton/Link.hpp>
#include <cmath>

LinkSession::LinkSession() : link_(std::make_unique<ableton::Link>(120.0)) {}
LinkSession::~LinkSession() { link_->enable(false); }

void LinkSession::enable(bool on) {
    link_->enable(on);
    enabled_.store(on);
}

void LinkSession::setTempo(double bpm) {
    auto st = link_->captureAppSessionState();
    st.setTempo(bpm, link_->clock().micros());
    link_->commitAppSessionState(st);
}

void LinkSession::info(double* o, int64_t outLatencyUs) {
    const auto t = link_->clock().micros() + std::chrono::microseconds(outLatencyUs);
    auto st = link_->captureAppSessionState();
    o[0] = link_->isEnabled() ? 1 : 0;
    o[1] = double(link_->numPeers());
    o[2] = st.tempo();
    o[3] = st.beatAtTime(t, kQuantum);
    o[4] = st.phaseAtTime(t, kQuantum);
}
