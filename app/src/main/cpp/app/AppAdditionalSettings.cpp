// Настройки устройства, общие для всех проектов.
#include "App.h"

namespace jambo {

void App::setAdditionalSettings(bool repeats, bool recordFromStart, bool stretchBeat, int sends, int click) {
    arrangeRepeats_ = repeats;
    arrangeRecordFromStart_ = recordFromStart;
    const int duck = sends & 7;
    if (stretchBeat != stretchBeat_ || duck != duckSends_) {
        stretchBeat_ = stretchBeat;
        duckSends_ = duck;
        publishAudio();
    }
    const int level = std::clamp(click, 0, 200);
    if (level != clickLevel_) {
        clickLevel_ = level;
        audio_->push(CoreEvent::metronomeGain(float(level) / 100.f));
    }
}

}  // namespace jambo
