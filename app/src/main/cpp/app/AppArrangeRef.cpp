// Референс-трек аранжировщика (AppModelArrangeRef.swift).
#include "App.h"
#include <algorithm>
#include <cmath>

namespace jambo {

void App::toggleArrangeReference() {
    if (!project_.arrange.ref) {
        openArrangeReference();
        return;
    }
    if (project_.arrange.showRef) arrangeRefB_ = false;
    edit([](Project& p) { p.arrange.showRef = !p.arrange.showRef; });
}

void App::openArrangeReference() {
    openBrowser({});
    browser_->reference = true;
    bump();
}

void App::loadArrangeReference(const std::string& key, const std::string& name) {
    const auto data = sampleData(key);
    if (!data) return;
    const double bpm = data->loopBpm();
    edit([&](Project& p) {
        p.files.try_emplace(key, name);
        p.arrange.ref = ArrRef{key, name, 0, 0, bpm};
        p.arrange.showRef = true;
    });
    closeBrowser();
    if (!arrangeView_) toggleArrange();
}

void App::deleteArrangeReference() {
    arrangeRefB_ = false;
    edit([](Project& p) {
        p.arrange.ref.reset();
        p.arrange.showRef = false;
    });
}

void App::toggleArrangeReferenceB() {
    if (!arrangeView_ || !project_.arrange.showRef || !project_.arrange.ref) return;
    arrangeRefB_ = !arrangeRefB_;
    publishAudio();
    bump();
}

void App::setArrangeReferenceOffset(double seconds) {
    if (!project_.arrange.ref || !std::isfinite(seconds)) return;
    edit([seconds](Project& p) { p.arrange.ref->offset = seconds; });
}

void App::setArrangeReferenceGain(double db) {
    if (!project_.arrange.ref || !std::isfinite(db)) return;
    edit([db](Project& p) { p.arrange.ref->gain = std::clamp(db, Params::volMin, 0.0); });
}

}  // namespace jambo
