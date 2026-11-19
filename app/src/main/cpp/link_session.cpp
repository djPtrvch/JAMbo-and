#include "link_session.h"
#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#include <cmath>

struct LinkSession::Filter { ableton::link::HostTimeFilter<ableton::Link::Clock> f; };

LinkSession::LinkSession()
    : link_(std::make_unique<ableton::Link>(120.0)), filter_(std::make_unique<Filter>()) {}

std::chrono::microseconds LinkSession::hostTimeAtFrame(double frame) {
    return filter_->f.sampleTimeToHostTime(frame);
}
void LinkSession::resetHostTime() { filter_->f.reset(); }
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

std::chrono::microseconds LinkSession::clockMicros() const { return link_->clock().micros(); }

void LinkSession::beatsAt(std::chrono::microseconds t0, std::chrono::microseconds t1,
                          double& b0, double& b1, double& phaseAtNextBeat) const {
    auto st = link_->captureAudioSessionState();
    b0 = st.beatAtTime(t0, kQuantum);
    b1 = st.beatAtTime(t1, kQuantum);
    const double next = std::floor(b0) + 1.0;
    phaseAtNextBeat = std::fmod(next, kQuantum);
    if (phaseAtNextBeat < 0) phaseAtNextBeat += kQuantum;
}
