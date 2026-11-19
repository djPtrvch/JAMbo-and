// MIDI OUT дорожки (порт Core/Sampler/SamplerMidi.swift): ноты дорожки с выходом не играют голоса, а уходят в очередь
// midiOut (её разбирает MidiOutPump вне колбэка). Всё — в колбэке, без выделений памяти.
#include <algorithm>
#include <cmath>
#include "Sampler.h"

namespace jambo {

bool Sampler::sendsMidi(int t) const {
    if (!sound_ || t < 0 || t >= sound_->trackCount) return false;
    return sound_->midiOut[size_t(t)] != 0;
}

void Sampler::sidechainTrig(int t, int64_t frame) {
    if (fx) fx->noteTrig(t, frame);
}

void Sampler::midiNotes(int t, int64_t frame, const int16_t* notes, int count, VoiceMask mask, const float* vals,
                        int64_t length, int live) {
    if (!sound_ || t < 0 || t >= sound_->trackCount || sound_->open[size_t(t)] == 0) return;
    const SoundData& d = *sound_;
    modFrame = frame;
    const double lv = pv(d, t, VoiceParam::level, mask, vals);
    const double v = std::max(0.0, std::min(127.0, std::round(lv)));
    if (v < 1) return;
    MidiNoteOut e;
    e.kind = MidiNoteOut::on;
    e.track = int32_t(t);
    e.velocity = uint8_t(v);
    e.frame = frame;
    e.length = length;
    e.liveId = live;
    for (int k = 0; k < count; ++k) {
        e.note = notes[k];
        midiOut.push(e);
    }
}

void Sampler::midiRelease(int live, int64_t frame) {
    MidiNoteOut e;
    e.kind = MidiNoteOut::release;
    e.frame = frame;
    e.liveId = live;
    midiOut.push(e);
}

void Sampler::midiAllOff(int64_t frame) {
    MidiNoteOut e;
    e.kind = MidiNoteOut::allOff;
    e.frame = frame;
    midiOut.push(e);
}

}  // namespace jambo
