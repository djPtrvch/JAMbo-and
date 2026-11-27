// Действия MIDI-целей (MidiButtons.swift и AppModelMidi.swift).
#include "App.h"
#include "midi/MidiTarget.h"
#include <algorithm>
#include <cmath>

namespace jambo {

namespace {
double midiValue(int value, int encoder, double low, double high, double current) {
    if (encoder == 0) return std::clamp(low + (high - low) * value / 127.0, 0.0, 1.0);
    const int step = encoder == 1 ? value - 64 : encoder == 2 ? (value < 64 ? value : value - 128)
        : ((value & 64) ? -(value & 63) : value & 63);
    return std::clamp(current + step * (high - low) / 127.0, 0.0, 1.0);
}
}  // namespace

void App::midiMappedKnob(int target, int value, int encoder, double low, double high) {
    namespace mt = MidiTarget;
    if (value < 0 || value > 127 || encoder < 0 || encoder > 3 || !std::isfinite(low) || !std::isfinite(high)) return;
    // Нулевое значение относительного энкодера не меняет параметр и не создаёт UNDO.
    if ((encoder == 1 && value == 64) || (encoder == 2 && value == 0) ||
        (encoder == 3 && (value & 63) == 0)) return;
    const auto normalized = [&](double current) { return midiValue(value, encoder, low, high, current); };
    if (target == mt::bpm) {
        const double span = Params::bpmMax - Params::bpmMin;
        setBpm(std::round(Params::bpmMin + span * normalized((project_.bpm - Params::bpmMin) / span)));
    } else if (target == mt::level) {
        const int ch = meterChannel();
        setMeterVol(ch, Params::volMin * (1 - normalized((meterVol(ch) - Params::volMin) / -Params::volMin)));
    } else if (mt::in(target, 0, mt::knobCount) && param(target)) {
        if (const auto source = assignSource())
            setAssign(target, *source, normalized((assignStart(target, *source) + 1) / 2) * 2 - 1);
        else
            setKnob(target, normalized(knobStart(target)));
    }
}

void App::midiMappedButton(int target, bool down, bool touch, bool setState, bool global) {
    namespace mt = MidiTarget;
    if (mt::in(target, mt::press, mt::knobCount)) {
        const int slot = target - mt::press;
        if (!touch) {
            if (down) {
                if (const auto source = assignSource()) clearAssign(slot, *source);
                else if (held_.empty() && !knobShift()) resetKnob(slot);
                else knobDoubleTap(slot);
            }
            return;
        }
        if (down) {
            const auto prior = midiKnobTaps_.find(slot);
            if (prior != midiKnobTaps_.end() && now_ - prior->second < 0.35) {
                midiKnobTaps_.erase(prior);
                if (const auto source = assignSource()) clearAssign(slot, *source);
                else knobDoubleTap(slot);
            } else {
                midiKnobTaps_[slot] = now_;
            }
        }
        knobFinger(slot, down);
        return;
    }
    if (mt::in(target, mt::lfo, 4)) {
        if (!down || channel_ != Channel::master) setSource(target - mt::lfo + 1, down, now_);
        return;
    }
    if (mt::in(target, mt::key, mt::keyCount)) {
        const int key = target - mt::key;
        const int id = mt::keysIdentity + key;
        if (down) {
            if (!pressedKeys_.count(id)) keyDown(id, key, 1, false);
        } else keyUp(id);
        return;
    }
    if (mt::in(target, mt::page, Params::maxPages)) {
        const int page = target - mt::page;
        if (!down) midiPagesDown_.erase(page);
        else if (page < int(seq().pages().size())) {
            midiPagesDown_.insert(page);
            int first = page, last = page;
            for (int p : midiPagesDown_) if (p < int(seq().pages().size())) {
                first = std::min(first, p);
                last = std::max(last, p);
            }
            playPages(first, last);
        }
        return;
    }
    if (mt::in(target, mt::step, Params::stepsPerPage)) {
        if (down) {
            const int index = viewPages().first * Params::stepsPerPage + target - mt::step;
            if (midiStepsDown_.emplace(target, std::make_pair(index, now_)).second) holdStep(index, true);
        } else if (const auto active = midiStepsDown_.find(target); active != midiStepsDown_.end()) {
            const auto [index, at] = active->second;
            midiStepsDown_.erase(active);
            if (held_.count(index)) {
                if (now_ - at < 0.35 && !stepUsed(index)) toggleStep(index);
                holdStep(index, false);
            }
        }
        return;
    }
    switch (target) {
    case mt::shift: setShift(down); return;
    case mt::fx:
    case mt::mod: {
        const auto page = target == mt::fx ? GridPage::fx : GridPage::mod;
        if (down) pagePress(page);
        else pageRelease(page);
        return;
    }
    case mt::morph:
        if (down) morphPress();
        else morphRelease();
        return;
    default: break;
    }
    if (setState) {
        switch (target) {
        case mt::play: if (down != playing_) togglePlay(); return;
        case mt::rec: if (down != recOn()) toggleRec(); return;
        case mt::metronome: if (down != metronome_) toggleMetronome(); return;
        case mt::eq: if (down != eqView_) toggleEqView(); return;
        case mt::arp: setArpMode(down); return;
        case mt::patterns: if (down != patternView_) togglePatterns(); return;
        default: break;
        }
    }
    if (!down) return;
    const int tracks = int(project_.tracks.size());
    if (mt::in(target, mt::track, tracks)) selectTrack(target - mt::track);
    else if (mt::in(target, mt::mute, tracks)) toggleMute(target - mt::mute);
    else if (mt::in(target, mt::solo, tracks)) toggleSolo(target - mt::solo);
    else if (mt::in(target, mt::arm, tracks)) stemTap(target - mt::arm);
    else if (mt::in(target, mt::chordType, int(Scales::chordKinds.size()))) chooseChordType(target - mt::chordType);
    else if (mt::in(target, mt::pageArrow, 4)) pageArrow(static_cast<PageAction>(target - mt::pageArrow));
    else if (mt::in(target, mt::row, mt::windowRows) || mt::in(target, mt::clip, tracks * mt::windowRows)) {
        if (!patternView_ && !global) return;
        const auto window = gridWindow();
        const bool scene = target < mt::clip;
        const int r = scene ? target - mt::row : (target - mt::clip) % mt::windowRows;
        if (r >= int(window.rows.size()) || window.rows[size_t(r)] < 0) return;
        if (scene) queueScene(window.rows[size_t(r)]);
        else tapCell((target - mt::clip) / mt::windowRows, window.rows[size_t(r)]);
    } else {
        switch (target) {
        case mt::play: togglePlay(); break;
        case mt::rec: toggleRec(); break;
        case mt::undo: undo(); break;
        case mt::redo: redo(); break;
        case mt::metronome: toggleMetronome(); break;
        case mt::eq: toggleEqView(); break;
        case mt::patterns: togglePatterns(); break;
        case mt::chord: chordTap(); break;
        case mt::arp: setArpMode(!arp().mode); break;
        case mt::octaveUp: setOctave(octave() + 1); break;
        case mt::octaveDown: setOctave(octave() - 1); break;
        case mt::mix: selectChannel(Channel::mix); break;
        case mt::master: selectChannel(Channel::master); break;
        case mt::armMaster: stemTap(StemRing::master); break;
        case mt::gridUp: if (patternView_ || global) gridArrow(-1); break;
        case mt::gridDown: if (patternView_ || global) gridArrow(1); break;
        case mt::soundLeft: folderArrow(-1); break;
        case mt::soundRight: folderArrow(1); break;
        default: break;
        }
    }
}

}  // namespace jambo
