// MIDI-клок привязан к тому же хост-времени, что слышимый шаг транспорта.
#include "App.h"
#include <algorithm>
#include <cmath>

namespace jambo {

double App::runningBpm() const { return midiBpm_.value_or(project_.bpm); }

double App::midiFrameAt(double heardHost) const {
    const auto c = audio_->clock();
    if (c.blockHost <= 0) return double(audio_->framesRendered());
    return c.blockFrame + (heardHost - c.blockHost) * 48000.0;
}

void App::startMidiPlay(double host) {
    playing_ = true;
    endRecTake();
    liveStopped();
    midiPhaseStep_ = -1;
    const auto frame = int64_t(std::llround(midiFrameAt(host)));
    audio_->push(CoreEvent::arrange(false, 0));
    audio_->push(CoreEvent::playAt(frame));
    bump();
}

void App::resetMidiClock() {
    midiClockFollower_.reset();
    midiPhaseStep_ = -1;
    midiContinue_ = false;
    if (midiBpm_) {
        midiBpm_.reset();
        audio_->push(CoreEvent::tempo(project_.bpm));
        bump();
    }
}

void App::midiRealtime(int status, double at, bool startStop, double offset) {
    if (!std::isfinite(at) || !std::isfinite(offset)) return;
    if (status == 0xF8) {
        const bool start = midiClockFollower_.tick(at);
        if (const auto bpm = midiClockFollower_.tempo(); bpm && *bpm >= Params::bpmMin && *bpm <= Params::bpmMax) {
            if (!midiBpm_ || std::abs(*bpm - *midiBpm_) >= 0.02) {
                const auto old = std::lround(runningBpm() * 10);
                midiBpm_ = bpm;
                audio_->push(CoreEvent::tempo(*bpm));
                if (old != std::lround(*bpm * 10)) bump();
            }
        }
        if (start || midiContinue_) {
            if (midiContinue_) midiClockFollower_.anchor(at);
            startMidiPlay(at + offset - audio_->outputLatency());
            midiContinue_ = false;
        }
    } else if (startStop) {
        if (status == 0xFA || status == 0xFC) {
            if (playing_) togglePlay();
            midiContinue_ = false;
            if (status == 0xFA) midiClockFollower_.arm();
            else midiClockFollower_.disarm();
        } else if (status == 0xFB && !playing_) {
            midiContinue_ = true;
        }
    }
}

ClockPlan::Batch App::tickMidiSync(double now, bool send, bool receive, bool startStop, double offset) {
    if (!std::isfinite(now) || !std::isfinite(offset)) return {};
    if (!receive || (midiClockFollower_.last() > 0 && now - midiClockFollower_.last() > ClockFollower::timeout))
        resetMidiClock();
    const auto c = audio_->clock();
    const double latency = audio_->outputLatency();
    midiOffset_ = offset;
    std::optional<ClockPlan::Grid> grid;
    if (playing_ && c.playing && c.blockHost > 0 && c.stepFrames > 0) {
        const double time = c.blockHost + (c.nextFrame - c.blockFrame) / 48000.0 + latency;
        grid = ClockPlan::Grid{c.nextTick, time, c.stepFrames / 48000.0};
        if (midiBpm_ && midiPhaseStep_ != c.nextTick) {
            if (!midiClockFollower_.anchored() || midiPhaseStep_ > c.nextTick)
                midiClockFollower_.anchor(time - c.nextTick * grid->stepDuration - offset);
            if (auto step = midiClockFollower_.stepTime(c.nextTick)) {
                audio_->push(CoreEvent::clockPhase(int(c.nextTick), midiFrameAt(*step + offset - latency)));
                midiPhaseStep_ = c.nextTick;
            }
        }
    } else {
        midiPhaseStep_ = -1;
    }
    if (!send) {
        if (!midiOutput_) return {};
        midiOutput_ = false;
        midiClockPlan_.reset();
        return {true, {}};
    }
    midiOutput_ = true;
    auto out = midiClockPlan_.update(now, runningBpm(), grid, startStop, 0.04 + std::max(0.0, -offset));
    for (auto& e : out.events) e.time += offset;
    return out;
}

void App::midiMappedKnob(int slot, int value, int encoder, double low, double high) {
    if (slot < 0 || slot >= 16 || value < 0 || value > 127 || !std::isfinite(low) || !std::isfinite(high)) return;
    if (encoder == 0) {
        setKnob(slot, std::clamp(low + (high - low) * value / 127.0, 0.0, 1.0));
        return;
    }
    const int step = encoder == 1 ? value - 64 : encoder == 2 ? (value < 64 ? value : value - 128)
        : ((value & 64) ? -(value & 63) : value & 63);
    if (step != 0) setKnob(slot, std::clamp(knobStart(slot) + step * (high - low) / 127.0, 0.0, 1.0));
}

void App::midiMappedButton(int target, bool down, bool touch, bool setState) {
    if (target >= 16 && target < 32) {
        if (touch) knobFinger(target - 16, down);
        else if (down) knobDoubleTap(target - 16);
        return;
    }
    if (target == 37) {
        setShift(down);
        return;
    }
    if (target == 38 || target == 39) {
        const auto page = target == 38 ? GridPage::fx : GridPage::mod;
        if (down) pagePress(page);
        else pageRelease(page);
        return;
    }
    if (target >= 48 && target < 64) {
        const int step = viewPage() * 16 + target - 48;
        if (down) toggleStep(step);
        return;
    }
    if (setState) {
        switch (target) {
        case 32: if (down != playing_) togglePlay(); return;
        case 33: if (down != recOn()) toggleRec(); return;
        case 36: if (down != metronome_) toggleMetronome(); return;
        case 40: if (down != eqView_) toggleEqView(); return;
        case 41: if (down != patternView_) togglePatterns(); return;
        }
    }
    if (!down) return;
    switch (target) {
    case 32: togglePlay(); break;
    case 33: toggleRec(); break;
    case 34: undo(); break;
    case 35: redo(); break;
    case 36: toggleMetronome(); break;
    case 40: toggleEqView(); break;
    case 41: togglePatterns(); break;
    }
}

}  // namespace jambo
