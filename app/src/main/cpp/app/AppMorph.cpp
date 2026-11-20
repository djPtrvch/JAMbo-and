// App: MORPH (порт части AppModelMod.swift, ветка swift JAMbo). Тап — режим: ручки меняют параметры выбранной дорожки
// целиком, без UNDO (жёлтая точка на дорожке); повторный тап — возврат всего, что меняли, на всех дорожках (сразу или
// плавно, SETTINGS → MORPH → RETURN). SHIFT + MORPH — на все дорожки, пока держат. MORPH + UNDO — сбросить изменения.
#include "App.h"
#include <cmath>

namespace jambo {

void App::editMorph(const std::function<void(Project&)>& change) {
    change(project_);
    history_.shiftGesture(change);
    changed();
}

void App::toggleMorph() {
    if (morph_) {
        const double sec = MorphReturn::seconds(morphReturn_, project_.bpm);
        const auto pts = project_.morphReturnPoints();
        if (sec > 0 && !pts.empty()) {
            morphGlide_ = MorphGlide{pts, now_, sec};
        } else {
            undoMorphEdits();
        }
    } else {
        stopMorphGlide();
    }
    morph_ = !morph_;
    bump();
}

void App::stepMorphGlide(double now) {
    if (!morphGlide_) return;
    const double f = (now - morphGlide_->start) / morphGlide_->seconds;
    if (f < 1) {
        const auto pts = morphGlide_->points;
        mutatePlay([&](Project& p) { p.applyMorphReturn(pts, f); }, true);
        return;
    }
    morphGlide_.reset();
    editMorph([](Project& p) {
        for (int i = 0; i < int(p.tracks.size()); ++i)
            if (p.tracks[size_t(i)].morphEdited) p.undoMorph(i);
    });
}

void App::stopMorphGlide() {
    if (!morphGlide_) return;
    morphGlide_.reset();
    changed();
}

void App::undoMorphEdits() {
    stopMorphGlide();
    editMorph([](Project& p) {
        for (int i = 0; i < int(p.tracks.size()); ++i)
            if (p.tracks[size_t(i)].morphEdited) p.undoMorph(i);
    });
}

void App::morphPress() {
    morphHeld_ = true;
    morphUsed_ = false;
    morphOffOnRelease_ = false;
    if (shiftHeld_) {
        beginMorphAllHold();
    } else if (morphHoldMode_) {
        morphUsed_ = true;
        if (!morph_) toggleMorph();
    }
    bump();
}

void App::beginMorphAllHold() {
    if (morphAllHeld_) return;
    morphUsed_ = true;
    morphAllHeld_ = true;
    if (!morph_) toggleMorph();
}

void App::endMorphAllHold() {
    morphAllHeld_ = false;
    if (morph_) toggleMorph();
}

void App::morphRelease() {
    morphHeld_ = false;
    bump();
    if (morphAllHeld_) {
        if (!shiftHeld_) endMorphAllHold();
        return;
    }
    if (morphOffOnRelease_) {
        morphOffOnRelease_ = false;
        if (morph_) toggleMorph();
        return;
    }
    if (morphHoldMode_) {
        if (morph_) toggleMorph();
        return;
    }
    if (!morphUsed_) toggleMorph();
}

void App::setShift(bool on) {
    shiftHeld_ = on;
    bump();
    if (on) {
        // При уже зажатой MORPH — режим на всех дорожках, пока держат обе.
        if (morphHeld_) beginMorphAllHold();
        return;
    }
    // Отпустили SHIFT: MORPH ещё держат — режим остаётся, ручки снова меняют выбранную дорожку; уже отпущена —
    // режим выключается.
    if (!morphAllHeld_) return;
    if (morphHeld_) {
        morphAllHeld_ = false;
        morphOffOnRelease_ = true;
    } else {
        endMorphAllHold();
    }
}

void App::undoTapped() {
    if (morphHeld_) {
        morphUsed_ = true;
        undoMorphEdits();
    } else {
        stopMorphGlide();
        undo();
    }
}

bool App::morphShown() const {
    return morphHeld_ && !morphUsed_ && !shiftHeld_ ? !morph_ : morph_;
}

}  // namespace jambo
