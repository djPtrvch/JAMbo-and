// Состояние приложения (app/App): правки шагов и ручек, UNDO, клавиатура, ход игры — через настоящее ядро.
#include <filesystem>
#include "app/App.h"
#include "midi/MidiTarget.h"
#include "model/SynthEngines.h"
#include "model/DrumRhythm.h"
#include "model/MelodyRhythm.h"
#include "model/SliceRhythm.h"
#include "model/Scales.h"
#include "engine/AudioCore.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jambo;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}

struct CoreAudio : AppAudio {
    AudioCore core{false};
    double now = 0;
    CoreAudio() { core.installSynth(); }
    void push(const CoreEvent& e) override { core.events.push(e); }
    void publish(const Project& p, const SoundSnapshot::Options& o,
                 const std::map<std::string, SampleRef>& samples) override {
        core.publish(p, samples, o, now);
    }
    StemRing* stemRing() override { return &core.sequencer.stemRing; }
    Levels* levels() override { return &core.levels; }
    Spectra* spectra() override { return &core.spectra; }
    PlayClock::State clock() const override { return core.sequencer.clock.read(); }
    double heardNow() const override { return now; }
    const Levels* levels() const override { return &core.levels; }
    bool popOut(SeqOut& e) override {
        auto x = core.sequencer.out.pop();
        if (!x) return false;
        e = *x;
        return true;
    }
    bool popArp(ArpOut& e) override {
        auto note = core.arp.out.pop();
        if (!note) return false;
        e = *note;
        return true;
    }
    void run(int frames) {
        std::vector<float> l(256), r(256);
        for (int pos = 0; pos < frames; pos += 256) core.render(256, l.data(), r.data());
    }
};

void steps() {
    CoreAudio a;
    App app(&a);
    app.toggleStep(0);
    const Step* s = app.project().tracks[0].step(0);
    check(s && s->on && s->notes == std::vector<int>{48}, "app: триг — корневая нота октавы (C3)");
    check(app.stepInfo(0, 0).on && app.stepInfo(0, 0).label == "C3", "app: шаг показывает ноту");
    app.undo();
    check(!app.project().tracks[0].step(0)->on && app.canRedo(), "app: UNDO снимает триг");
    app.redo();
    check(app.project().tracks[0].step(0)->on, "app: REDO возвращает");
    // Протягивание — одна запись UNDO на жест.
    app.beginGesture();
    app.setNoteLength(4, 2);
    app.setNoteLength(4, 3);
    app.endGesture();
    check(app.project().tracks[0].step(4)->len == 3 && app.stepInfo(0, 5).tail && app.stepInfo(0, 6).tail &&
              !app.stepInfo(0, 7).tail,
          "app: длина ноты — хвост");
    app.undo();
    check(!app.project().tracks[0].step(4)->on, "app: протягивание — одна запись UNDO");
    check(app.copyStep(0, 8) && app.project().tracks[0].step(8)->on, "app: копия шага");
    app.restoreCopy(8);
    check(!app.project().tracks[0].step(8)->on, "app: отмена копии");
    app.setPatternLength(11);
    check(app.stepInfo(0, 12).outside && app.stepInfo(0, 11).last, "app: длина паттерна");
}

void knobs() {
    CoreAudio a;
    App app(&a);
    app.selectTrack(1);
    const KnobState f = app.knobState(6);  // 2.3 Filter
    check(f.param && f.param->id == "filter", "app: ручка 2.3 — Filter");
    app.beginGesture();
    app.setKnob(4, 0.5);  // Attack
    app.endGesture();
    const double v = app.project().tracks[1].params.at("attack");
    check(std::fabs(app.knobState(4).norm - 0.5) < 1e-6 && v > 0, "app: ручка меняет параметр дорожки");
    app.toggleStep(3);
    app.holdStep(3, true);
    app.setKnob(4, 0.9);
    check(app.knobState(4).locked && app.project().tracks[1].step(3)->locks.count("attack") &&
              std::fabs(app.project().tracks[1].params.at("attack") - v) < 1e-9 && app.stepUsed(3),
          "app: с зажатым шагом — p-lock");
    app.knobDoubleTap(4);
    check(!app.project().tracks[1].step(3)->locks.count("attack"), "app: двойной тап снимает p-lock");
    app.holdStep(3, false);
    app.selectChannel(Channel::mix);
    check(app.knobState(0).label == "1" && !app.knobState(9).param, "app: MIX — громкости, лишние серые");
    app.setKnob(0, 1);
    check(app.project().tracks[0].vol == 0, "app: MIX — громкость дорожки");
}

void keysAndPlay() {
    CoreAudio a;
    App app(&a);
    check(app.keyCount() == 8 && app.keyNote(0) == 48, "app: клавиатура — октава от C3");
    app.keyDown(7, 2, 1);
    check(app.litNotes().count(app.keyNote(2)) && app.shownNotes() == std::set<int>{app.keyNote(2)},
          "app: нажатая нота подсвечена и выбрана");
    app.keyUp(7);
    app.toggleStep(0);
    check(app.project().tracks[0].step(0)->notes == std::vector<int>{app.keyNote(2)}, "app: триг — выбранной нотой");
    app.setOctave(app.octave() + 1);
    check(app.keyNote(0) == 60, "app: октава вверх");
    for (int s = 0; s < 16; ++s) app.toggleStep(s == 0 ? 99 : s);
    app.togglePlay();
    a.run(13000);
    app.tick(1);
    check(app.playing() && app.openPlayPos() == 2 && (app.flashMask() & 1u), "app: ход игры и мигание");
    app.swipe(0, true);
    check(app.project().tracks[0].mute, "app: свайп — mute");
    const uint64_t r = app.revision();
    app.togglePlay();
    check(!app.playing() && app.openPlayPos() == -1 && app.revision() != r, "app: стоп");
}
void sound() {
    CoreAudio a;
    App app(&a);
    check(app.soundInfo().engine == -1 && app.soundInfo().title == "No sample", "app: без звука — No sample");
    app.openSoundMenu();
    check(app.soundMenu() && app.soundMenu()->empty(), "app: меню звука дорожки");
    app.pickSynth(17);
    const auto info = app.soundInfo();
    check(!app.soundMenu() && app.project().tracks[0].sample == "mi:17" && info.title == "ANALOG" &&
              info.rows.size() == 4 && info.rows[0].rfind("Wave A \u00b7 ", 0) == 0,
          "app: ANALOG на дорожку — окно синта");
    app.undo();
    check(app.project().tracks[0].sample.empty(), "app: UNDO выбора движка");
    // Зажатые шаги: движок — их свой звук; TRACK — снова звук дорожки.
    app.toggleStep(3);
    app.holdStep(3, true);
    app.openSoundMenu();
    app.holdStep(3, false);
    check(app.soundMenu() && *app.soundMenu() == std::vector<int>{3}, "app: меню звука шагов");
    app.pickSynth(18);
    check(app.project().tracks[0].step(3)->sound == std::optional<std::string>("mi:18"), "app: свой звук шага");
    app.holdStep(3, true);
    check(app.soundInfo().title == "FM", "app: зажат шаг — окно показывает его звук");
    app.openSoundMenu();
    app.stepSoundToTrack();
    app.holdStep(3, false);
    check(!app.project().tracks[0].step(3)->sound, "app: TRACK — звук дорожки");
    // WTABLE звучит: таблица дорожки уходит в снимок звука.
    {
        CoreAudio b;
        App w(&b);
        w.openSoundMenu();
        w.pickSynth(19);
        w.keyDown(1, 0, 1);
        std::vector<float> l(256), r(256);
        float peak = 0;
        for (int k = 0; k < 40; ++k) {
            b.core.render(256, l.data(), r.data());
            for (float x : l) peak = std::max(peak, std::abs(x));
        }
        w.keyUp(1);
        check(peak > 0.01f, "app: WTABLE звучит (таблица в снимке)");
    }
    app.openSoundMenu();
    app.cleanSampler();
    check(app.project().tracks[0].sample.empty() && !app.soundMenu(), "app: SAMPLER — без звука");
}
void mod() {
    CoreAudio a;
    App app(&a);
    app.openSoundMenu();
    app.pickSynth(17);
    // Держат LFO2, поворот ручки 2.3 (Filter) — назначение со степенью; двойной тап с LFO2 — снять.
    app.setSource(2, true, 0);
    check(app.assignSource() == 2, "app: держат LFO2 — источник для ручки");
    app.knobFinger(6, true);
    app.setAssign(6, 2, 0.5);
    check(app.assign(6) && app.assign(6)->lfo == 2 && app.assign(6)->amount == 0.5, "app: назначение LFO2 на ручку");
    check(app.knobState(6).heldMatch && app.fingerSources() == std::set<int>{2}, "app: ручка показывает степень");
    // Тап по другому источнику, пока держат ручку, — назначение переходит к нему.
    app.setSource(2, false, 0.1);
    app.setSource(3, true, 0.2);
    check(app.assign(6)->lfo == 3 && app.assign(6)->amount == 0.5, "app: назначение переходит к LFO3");
    app.knobFinger(6, false);
    app.setSource(3, false, 0.3);
    check(!app.assignSource() && app.modulatedNorm(6).has_value(), "app: отпустили — кольцо модуляции");
    app.clearAssign(6, 2);
    check(app.assign(6).has_value(), "app: снять можно только тем же источником");
    app.clearAssign(6, 3);
    check(!app.assign(6) && !app.anyAssign(), "app: назначение снято");
    app.undo();
    check(app.assign(6).has_value(), "app: UNDO возвращает назначение");
}
void pages() {
    CoreAudio a;
    App app(&a);
    app.toggleStep(2);
    app.addPage();
    check(app.shownSeq().pages().size() == 2 && app.viewPage() == 1, "app: + — новая страница открыта");
    app.playPages(0, 1);
    check(app.viewPages() == std::make_pair(0, 1) && app.shownSeq().playTo() == 1, "app: две играющие страницы");
    app.openPage(1);
    check(app.viewPages() == std::make_pair(1, 1), "app: удержание — одна открытая страница");
    app.playPages(0, 0);
    app.pageAction(App::PageAction::copy, 0);
    app.pageAction(App::PageAction::paste, 1);
    check(app.thumb(1)[2].on && app.hasPageClipboard(), "app: copy / paste страницы");
    app.pageAction(App::PageAction::clear, 0);
    check(!app.thumb(0)[2].on, "app: clear страницы");
    app.movePage(1, 0);
    check(app.thumb(0)[2].on && app.viewPage() == 0, "app: перенос страницы");
    app.pageArrow(App::PageAction::right);
    check(app.thumb(0)[3].on, "app: стрелка ▶ — шаги вправо");
    app.pageAction(App::PageAction::del, 1);
    check(app.shownSeq().pages().size() == 1 && !app.canDelete(0), "app: delete страницы");
    app.setClipRate(Clip::rates()[0]);
    check(app.clipRate() == Clip::rates()[0], "app: SPEED клипа");
}
void interruptedScreenKeys() {
    CoreAudio audio;
    App app(&audio);
    app.keyDown(1, 0, 1);
    app.selectTrack(1);
    app.keyDown(2, 1, 1);
    const int mapped = MidiTarget::key + 2;
    app.midiMappedButton(mapped, true, false, false);
    const int midiNote = app.keyNote(2);
    check(app.litNotes().size() == 3, "keyboard: two screen touches and one MIDI mapping illuminate the keyboard");
    while (audio.core.events.pop()) {}

    app.releaseScreenKeys();
    check(app.litNotes() == std::set<int>{midiNote}, "keyboard: backgrounding clears only screen touches");
    const auto first = audio.core.events.pop();
    const auto second = audio.core.events.pop();
    check(first && first->kind == CoreEvent::Kind::noteOff && first->a == 0 && first->c == 1 &&
          second && second->kind == CoreEvent::Kind::noteOff && second->a == 1 && second->c == 2,
          "keyboard: cancelled touches release their voices on the tracks that started them");
    app.releaseScreenKeys();
    app.keyUp(1);
    app.keyUp(2);
    check(!audio.core.events.pop(), "keyboard: late Android release after cancellation does not release twice");

    app.keyDown(1, 0, 1);
    check(app.litNotes().size() == 2, "keyboard: a new touch plays on return");
    app.keyUp(1);
    app.midiMappedButton(mapped, false, false, false);
    check(app.litNotes().empty(), "keyboard: both normal screen and MIDI note-off still work");
}

void undoRedoPages() {
    CoreAudio audio;
    App app(&audio);
    Project p;
    p.tracks[0].addPage();
    p.tracks[1].addPage();
    app.loadProject(p);

    app.openPage(0);
    app.toggleStep(0);
    app.openPage(1);
    app.beginGesture();
    app.toggleStep(16);
    app.endGesture();
    app.selectTrack(1);
    app.openPage(0);
    app.toggleStep(1);

    app.undo();
    check(app.selectedTrack() == 1 && app.viewPage() == 0 && !app.project().tracks[1].step(1)->on,
          "history: UNDO shows the edited track and page");
    app.undo();
    check(app.selectedTrack() == 0 && app.viewPage() == 1 && !app.project().tracks[0].step(16)->on,
          "history: UNDO shows the page of a gesture");
    app.undo();
    check(app.viewPage() == 0 && !app.project().tracks[0].step(0)->on, "history: UNDO goes back to page one");
    app.redo();
    check(app.viewPage() == 0 && app.project().tracks[0].step(0)->on, "history: REDO restores page one");
    app.redo();
    check(app.viewPage() == 1 && app.project().tracks[0].step(16)->on, "history: REDO goes forward to page two");
    app.redo();
    check(app.selectedTrack() == 1 && app.viewPage() == 0 && app.project().tracks[1].step(1)->on,
          "history: REDO shows the other track and page");

    app.setLatched(true, false);
    app.pagePress(App::GridPage::fx);
    app.toggleStep(2);
    app.pagePress(App::GridPage::fx);
    app.undo();
    check(app.fxMode(), "history: UNDO shows latched FX");
    app.pagePress(App::GridPage::fx);
    app.redo();
    check(app.fxMode(), "history: REDO shows latched FX");

    p.rows = 2;
    p.fillRows();
    app.loadProject(p);
    app.holdCell(0, 1);
    app.toggleStep(2);
    app.holdCell(0, 0);
    app.undo();
    check(app.project().tracks[0].open == 1 && !app.project().tracks[0].step(2)->on,
          "history: UNDO reopens the edited clip");
    app.holdCell(0, 0);
    app.redo();
    check(app.project().tracks[0].open == 1 && app.project().tracks[0].step(2)->on,
          "history: REDO reopens the edited clip");
}

void reorderPresets() {
    const auto dir = std::filesystem::temp_directory_path() / ("jambo-preset-order-" + Library::newId());
    CoreAudio audio;
    App app(&audio);
    app.openLibraryStorage(dir.string());
    for (const std::string name : {"A", "B", "C"}) {
        app.saveEqPreset(name);
        for (int m = 0; m < 4; ++m) app.saveMasterPreset(m, name);
    }
    app.moveEqPreset(0, 2);
    app.moveEqPreset(2, 1);
    for (int m = 0; m < 4; ++m) {
        app.moveMasterPreset(m, 0, 2);
        app.moveMasterPreset(m, 2, 1);
    }

    CoreAudio reopenedAudio;
    App reopened(&reopenedAudio);
    reopened.openLibraryStorage(dir.string());
    const auto matches = [](const auto& list) {
        return list.size() == 3 && list[0].name == "B" && list[1].name == "A" && list[2].name == "C";
    };
    check(matches(reopened.eqPresets().list), "presets: EQ order persists after moving down and up");
    for (int m = 0; m < 4; ++m)
        check(matches(reopened.masterPresets().list(m)), "presets: master module order persists after moving down and up");
    std::filesystem::remove_all(dir);
}

void fxPages() {
    CoreAudio a;
    App app(&a);
    app.openSoundMenu();
    app.pickSynth(17);
    // FX — удержание: ручки — общие эффекты; поворот — FX клипа дорожки.
    app.pagePress(App::GridPage::fx);
    check(app.fxMode() && app.knobState(0).param && app.knobState(0).param->id.rfind("rev.", 0) == 0, "app: FX — ручки эффектов");
    app.setKnob(0, 0.9);
    check(std::abs(app.project().fxValue(0, app.knobState(0).param->id) - app.knobState(0).param->fromNorm(0.9)) < 1e-9,
          "app: FX — поворот меняет эффект");
    // LFO на ручку FX — назначение в fxAssigns с дорожкой-источником.
    app.setSource(1, true, 0);
    app.setAssign(0, 1, 0.3);
    app.setSource(1, false, 0.1);
    const std::string id0 = app.knobState(0).param->id;
    check(app.project().fxAssigns.count(id0) && app.project().fxAssigns.at(id0).track == 0, "app: LFO на ручку FX");
    app.pageRelease(App::GridPage::fx);
    check(!app.fxMode() && app.knobState(0).param->id == "harm", "app: отпустили FX — ручки дорожки");
    // SHIFT + нажатие MOD — режим toggle; тап — вкл., ещё тап — выкл.
    app.setShift(true);
    app.pagePress(App::GridPage::mod);
    app.setShift(false);
    check(app.latched(App::GridPage::mod) && !app.modMode(), "app: SHIFT + MOD — toggle");
    app.pagePress(App::GridPage::mod);
    app.pageRelease(App::GridPage::mod);
    check(app.modMode() && !app.knobState(0).assign, "app: MOD включён тапом, назначений нет");
    app.pagePress(App::GridPage::mod);
    check(!app.modMode(), "app: MOD выключен тапом");
    const auto l1 = app.modMini(1), en = app.modMini(4);
    check(l1.xs.size() == 101 && l1.title.rfind("LFO1 \u00b7 ", 0) == 0 && en.title.rfind("ENV \u00b7 A ", 0) == 0 &&
              en.xs.size() == en.ys.size() && en.dotY >= 0 && en.dotY <= 1,
          "app: миниатюры LFO и ENV");
    // SHIFT + тап по Filter — следующий тип; SHIFT + двойной тап — снять все p-lock.
    int fs = 0;
    for (int k = 0; k < 16; ++k)
        if (app.knobState(k).param && app.knobState(k).param->id == "filter") fs = k;
    const ParamDef* f0 = app.knobState(fs).param;
    check(app.knobShiftTap(fs) && app.knobState(fs).param != f0, "app: SHIFT + тап по Filter — тип");
    app.toggleStep(0);
    app.holdStep(0, true);
    app.setKnob(1, 0.2);
    app.holdStep(0, false);
    check(!app.project().tracks[0].step(0)->locks.empty(), "app: p-lock поставлен");
    app.setShift(true);
    app.knobDoubleTap(1);
    app.setShift(false);
    check(app.project().tracks[0].step(0)->locks.empty(), "app: SHIFT + двойной тап — все p-lock сняты");
}
void morphApp() {
    CoreAudio a;
    App app(&a);
    const double f0 = app.knobState(6).norm;
    // Тап MORPH — режим; ручка меняет дорожку без UNDO; точка MORPH на дорожке.
    app.morphPress();
    app.morphRelease();
    check(app.morph(), "app: MORPH включён тапом");
    app.setKnob(6, 0.9);
    check(app.project().tracks[0].morphEdited && !app.canUndo(), "app: MORPH — без UNDO, точка на дорожке");
    app.morphPress();
    app.morphRelease();
    check(!app.morph() && std::abs(app.knobState(6).norm - f0) < 1e-9 && !app.project().tracks[0].morphEdited,
          "app: MORPH выключен — всё вернулось");
    // SHIFT + MORPH — на все дорожки, пока держат.
    app.setShift(true);
    app.morphPress();
    check(app.morph() && !app.knobShift(), "app: SHIFT + MORPH — все дорожки, SHIFT занята");
    app.setKnob(6, 0.8);
    check(app.project().tracks[1].morphEdited, "app: SHIFT + MORPH — соседняя дорожка тоже");
    app.morphRelease();
    app.setShift(false);
    check(!app.morph() && !app.project().tracks[1].morphEdited, "app: отпустили обе — режим выключен, возврат");
    // MORPH удержание + UNDO — сбросить изменения, режим остаётся.
    app.morphPress();
    app.morphRelease();
    app.setKnob(6, 0.7);
    app.morphPress();
    app.undoTapped();
    app.morphRelease();
    check(app.morph() && !app.project().tracks[0].morphEdited, "app: MORPH + UNDO — сброс, режим остаётся");
    // Обычная правка, затем MORPH: UNDO не трогает накрученное в MORPH.
    app.morphPress();
    app.morphRelease();
    app.toggleStep(0);
    app.morphPress();
    app.morphRelease();
    app.setKnob(6, 0.6);
    app.undoTapped();
    check(!app.project().tracks[0].step(0)->on && std::abs(app.knobState(6).norm - 0.6) < 1e-6,
          "app: UNDO не трогает MORPH");
}
void meter() {
    CoreAudio a;
    App app(&a);
    app.openSoundMenu();
    app.pickSynth(17);
    app.keyDown(1, 0, 1);
    std::vector<float> l(256), r(256);
    for (int k = 0; k < 40; ++k) a.core.render(256, l.data(), r.data());
    app.tick(1);
    const auto b = app.meters().bars(app.meterChannel(), app.trackStereo(0));
    check(app.meterChannel() == 0 && b.size() == 1 && b[0].level > 0.001f && app.meters().maxPeak[0] > 0.001f,
          "app: измеритель — уровень дорожки (моно)");
    app.keyUp(1);
    app.resetMeterPeak();
    check(app.meters().maxPeak[0] == 0, "app: тап по пику — сброс");
    app.setVolDrag(true);
    app.setMeterVol(0, -20);
    app.setMeterVol(0, -24);
    app.setVolDrag(false);
    check(app.project().tracks[0].vol == -24 && app.meterVol(0) == -24, "app: регулятор — громкость дорожки");
    app.undo();
    check(app.project().tracks[0].vol == Params::volDef, "app: регулятор — одна запись UNDO");
    app.selectChannel(Channel::master);
    app.setMeterVol(app.meterChannel(), -6);
    check(app.meterChannel() == Levels::master && app.project().outVol == -6, "app: MST — громкость выхода");
}
void rec() {
    CoreAudio a;
    App app(&a);
    app.openSoundMenu();
    app.pickSynth(17);
    std::vector<float> l(256), r(256);
    app.togglePlay();
    // ~0,6 с при 120 BPM — пятый шаг (шаг 125 мс).
    for (int k = 0; k < 110; ++k) a.core.render(256, l.data(), r.data());
    app.tick(1);
    app.toggleRec();
    const int before = int(app.canUndo());
    app.keyDown(1, 2, 1);
    app.keyUp(1);
    int on = -1;
    for (int i = 0; i < 16; ++i)
        if (app.project().tracks[0].step(i) && app.project().tracks[0].step(i)->on) on = i;
    check(on >= 3 && on <= 6 && app.project().tracks[0].step(on)->notes == std::vector<int>{app.keyNote(2)},
          "app: REC — нота на слышимом шаге");
    for (int k = 0; k < 50; ++k) a.core.render(256, l.data(), r.data());
    app.tick(1.3);
    app.keyDown(2, 4, 1);
    app.keyUp(2);
    int count = 0;
    for (int i = 0; i < 16; ++i)
        if (app.project().tracks[0].step(i) && app.project().tracks[0].step(i)->on) ++count;
    (void)before;
    check(count == 2, "app: REC — вторая нота в том же дубле");
    app.undo();
    count = 0;
    for (int i = 0; i < 16; ++i)
        if (app.project().tracks[0].step(i) && app.project().tracks[0].step(i)->on) ++count;
    check(count == 0, "app: UNDO отменяет весь дубль");
    app.toggleRec();
    app.togglePlay();
}
}  // namespace

void arpRecording() {
    CoreAudio audio;
    App app(&audio);
    Project p;
    p.tracks[0].sample = SynthKey::key(J::analog);
    app.loadProject(p);
    app.midiMappedButton(MidiTarget::chord, true, false, false);
    app.midiMappedButton(MidiTarget::arp, true, false, false);
    app.midiMappedButton(MidiTarget::chordType + Arp::down, true, false, false);
    app.setArpRate(4);
    app.setArpOctaves(2);
    app.setArpGate(0.8);
    app.setArpLatch(true);
    check(app.arp().active() && app.arp().dir == Arp::down && !app.chordOn() && !app.canUndo(),
          "arp: MIDI CHORD and CHORD TYPE, settings stay outside UNDO");

    app.toggleRec();
    app.togglePlay();
    app.keyDown(10, 0, 1);
    app.midiNoteFrom(55, 127, 999);
    check(!app.project().tracks[0].step(0)->on, "arp: REC waits for emitted notes");
    for (int i = 0; i < 12; ++i) {
        audio.run(1024);
        app.tick(1 + i * 0.02);
    }
    app.keyUp(10);
    app.midiNoteFrom(55, 0, 999);
    audio.run(8192);
    app.tick(1.5);
    int count = 0;
    std::set<int> notes;
    for (const Step& step : app.project().tracks[0].pages()[0]) {
        if (!step.on) continue;
        ++count;
        notes.insert(step.notes.begin(), step.notes.end());
        check(step.notes.size() == 1, "arp: REC keeps one emitted note per step");
    }
    check(count >= 3 && (notes.count(55) || notes.count(67)), "arp: records keyboard and external MIDI through LATCH");
    app.toggleRec();
    app.undo();
    check(!app.project().tracks[0].step(0)->on && app.arp().latch && app.arp().octaves == 2 && app.arp().gate == 0.8,
          "arp: UNDO removes the take and keeps ARP settings");
    app.loadProject(p);
    audio.run(256);
    check(!audio.core.arp.active(0), "arp: project load clears the previous arpeggiator");
}

void randomSwiftRules() {
    RhythmRandom::Rng rng(1944);
    const auto beforeFill = [](const auto& page, int at) {
        std::vector<std::pair<int, double>> out;
        for (const auto& e : page) {
            if (e.step < at) out.push_back({e.step, e.velocity.value_or(-1)});
        }
        return out;
    };
    bool twoChanges = false, keptBaseVelocity = true, finalFill = true, keptAnchors = true;
    bool correctGhosts = true, ghostPresent = false;
    for (int style = 0; style < 6; ++style) {
        for (int role = DrumRhythm::kick; role <= DrumRhythm::perc; ++role) {
            for (int trial = 0; trial < 60; ++trial) {
                const auto pages = DrumRhythm::pattern(role, style, {16, 16, 16, 7}, nullptr, rng);
                const auto base = DrumRhythm::steps(pages[0]);
                keptBaseVelocity &= beforeFill(pages[0], 3) == beforeFill(pages[3], 3);
                finalFill &= std::any_of(pages[3].begin(), pages[3].end(), [](const auto& hit) {
                    return hit.step >= 3 && hit.step < 7;
                });
                for (size_t i : {1, 2}) {
                    const auto steps = DrumRhythm::steps(pages[i]);
                    std::vector<int> difference;
                    std::set_symmetric_difference(base.begin(), base.end(), steps.begin(), steps.end(),
                                                  std::back_inserter(difference));
                    twoChanges |= difference.size() == 2;
                    keptAnchors &= difference.size() <= 2 && (role != DrumRhythm::kick || steps.front() == 0);
                }
                if (style == 0 && role == DrumRhythm::snare) {
                    for (const auto& hit : pages.front()) {
                        if (hit.step == 3) { ghostPresent = true; correctGhosts &= hit.velocity && *hit.velocity <= 57; }
                    }
                }
            }
        }
    }
    check(twoChanges && keptAnchors && keptBaseVelocity && finalFill && correctGhosts && ghostPresent,
          "RANDOM Swift: drum variation, preserved anchors and velocity, ghost hits and fill");

    std::vector<SliceRhythm::SlicePiece> pieces;
    for (int i = 0; i < 6; ++i) pieces.push_back({60 + i, 2, 0, 0, -1});
    bool breakFill = true, replacedOnlyNotes = true;
    for (int trial = 0; trial < 100; ++trial) {
        const auto pages = SliceRhythm::pattern(pieces, {16, 16, 16, 7}, 0, nullptr, rng);
        for (size_t i : {1, 2}) {
            replacedOnlyNotes &= pages[i].size() == pages[0].size() && pages[i].front().note == 60;
            for (size_t j = 0; j < pages[i].size(); ++j)
                replacedOnlyNotes &= pages[i][j].step == pages[0][j].step;
        }
        breakFill &= pages.back().front().note == 60 && pages.back().back().step >= 3 &&
                     pages.back().back().length == 1;
    }
    check(replacedOnlyNotes && breakFill, "RANDOM Swift: slice variations reuse the break and end with a fill");

    using MelodyRhythm::Role;
    TrackArp settings;
    settings.octaves = 2;
    bool cycles = true, clipped = true, rests = false;
    for (int trial = 0; trial < 100; ++trial) {
        const auto pat = MelodyRhythm::generate({Role::arp, 1, 0, 0, 0, 4, false, settings},
                                                {16, 16, 16, 7}, {1, 7}, {}, rng);
        rests |= pat.front().size() < 12;
        const std::array<int, 6> rows[] = {{60, 67, 71, 72, 79, 83}, {71, 77, 81, 83, 89, 93}};
        std::vector<int> positions;
        for (size_t p = 0; p < pat.size(); ++p) {
            const auto& row = rows[p / 2];
            for (const auto& e : pat[p]) {
                const auto at = std::find(row.begin(), row.end(), e.note);
                clipped &= e.step % 4 != 0 && e.step < (p == 3 ? 7 : 16) && at != row.end();
                positions.push_back(int(at - row.begin()));
            }
        }
        bool period = false;
        for (size_t n = 3; n <= 7 && n < positions.size(); ++n)
            period |= std::equal(positions.begin() + n, positions.end(), positions.begin());
        cycles &= period;
    }
    check(cycles && clipped && rests, "RANDOM Swift: Techno ARP keeps its 3-7 note cycle across pages and chord changes");

    bool bounded = true;
    for (int style = 0; style < 6; ++style) {
        for (Role role : {Role::bass, Role::lead, Role::keys, Role::pad, Role::arp}) {
            for (int scale = 0; scale < int(Scales::all.size()); ++scale) {
                const auto degrees = MelodyRhythm::progression(style, scale, nullptr, rng);
                const int kind = MelodyRhythm::kind(style, rng);
                const auto pat = MelodyRhythm::generate({role, style, kind, scale, 11, 6, false, settings},
                                                        {16, 16, 7, 0}, degrees, {}, rng);
                bounded &= pat.size() == 4 && pat[3].empty();
                for (size_t p = 0; p < 3; ++p)
                    for (const auto& event : pat[p])
                        bounded &= event.step >= 0 && event.step < (p == 2 ? 7 : 16) &&
                                   event.note >= 0 && event.note <= 127 && event.length > 0 && event.length <= 32;
            }
        }
    }
    check(bounded, "RANDOM Swift: all styles and roles honor note bounds and short pages");
    const MelodyRhythm::Kicks kicks{{0, 10}, {0, 6, 13}};
    const auto trap = MelodyRhythm::generate({Role::bass, 3, 0, 0, 0, 1, false, {}},
                                            {16, 16}, {1, 5}, kicks, rng);
    check(trap[0].size() == 2 && trap[0][0].step == 0 && trap[0][1].step == 10 &&
          trap[1].size() == 2 && trap[1][0].step == 0 && trap[1][1].step == 6,
          "RANDOM Swift: Trap bass follows the kick and leaves the last four steps clear");
}

void randomPatterns() {
    CoreAudio audio;
    App app(&audio);
    Project p;
    p.tracks[0].sample = SynthKey::key(J::analog);
    p.tracks[1].sample = SynthKey::key(J::drum);
    p.tracks[0].keyOctave = 2;
    for (size_t i = 0; i < 2; ++i) {
        auto& t = p.tracks[i];
        t.pagesMut().push_back(emptyPage());
        t.setLength(7);
        t.pagesMut()[1][0].on = true;
        t.pagesMut()[1][0].notes = {61};
    }
    app.loadProject(p);
    app.setShift(true);
    app.chordTap();
    check(app.project() == p && !app.canUndo(), "random: all options disabled by default");
    app.setRandomSettings(1 | 2 | 16 | 32);
    app.chooseRandomPart(1);
    check(app.randomPart() == 1, "random: PART overrides automatic bass role");
    app.chordTap();
    app.setShift(false);
    const auto& generated = app.project();
    for (size_t i = 0; i < 2; ++i) {
        const auto& pages = generated.tracks[i].pages();
        check(std::any_of(pages[0].begin(), pages[0].begin() + 7, [](const auto& s) { return s.on; }) &&
              std::none_of(pages[0].begin() + 7, pages[0].end(), [](const auto& s) { return s.on; }) &&
              pages[1] == p.tracks[i].pages()[1], "random: ensemble respects selected range and short pages");
    }
    for (int i = 0; i < 7; ++i)
        check(!(generated.tracks[1].step(i)->on && generated.tracks[0].step(i)->on),
              "random: lead leaves room for the kick generated by the ensemble");
    app.undo();
    check(app.project().tracks[0].pages() == p.tracks[0].pages() &&
          app.project().tracks[1].pages() == p.tracks[1].pages() && !app.canUndo(), "random: one UNDO for the ensemble");
}

void chords() {
    CoreAudio a;
    App app(&a);
    app.setScale(1);
    app.setRoot(9);
    check(app.project().scale == 1 && app.project().root == 9 && app.keyNote(0) == 57, "app: гамма и нота — A Minor");
    app.undo();
    check(app.project().root == 0 && app.project().scale == 1, "app: UNDO ноты");
    app.setScale(0);
    app.toggleChord();
    check(app.chordOn() && *app.keyNotes(0) == std::vector<int>{48, 52, 55}, "app: CHORD — трезвучие C");
    check(app.chordKeyLabel(1) == "Dm · ii\nD3", "app: подпись клавиши Dm · ii");
    app.setChordKind(1);
    check(app.keyNotes(4)->size() == 4 && app.chordKeyLabel(4).rfind("G7", 0) == 0, "app: тип 7th — G7");
    app.selectTrack(1);
    check(!app.chordOn() && app.keyNotes(0)->size() == 1, "app: CHORD — у каждой дорожки свой");
    app.setPianoSettings(2, true);
    check(app.keyCount() == 15, "app: 2 OCTAVES");
    app.setSwing(60);
    check(app.trackSwing() == 60 && app.canUndo(), "app: SWING дорожки");
}

void eq() {
    CoreAudio a;
    App app(&a);
    app.toggleEqView();
    check(app.eqView() && a.core.spectra.channel.load() == 0, "eq: окно — канал дорожки 1");
    app.addEqBand(1000, 6);
    check(app.eqBand() == 0 && app.selectedBand() && app.selectedBand()->gain == 6, "eq: новая bell-полоса выбрана");
    app.beginGesture();
    app.setEqBand(0, 2000, 3.0);
    app.setEqBand(0, 3000, 2.0);
    app.endGesture();
    app.undo();
    check(app.selectedBand() && app.selectedBand()->freq == 1000, "eq: жест — одна запись UNDO");
    const auto c = eqCurves(app.eqTarget(), 64, app.eqDynOffsets());
    double peak = 0;
    for (double v : c.stereo) peak = std::max(peak, v);
    check(c.hasStereo && peak > 5 && peak < 6.5, "eq: кривая — пик около +6 dB");
    std::vector<float> pre, post;
    a.run(4096);
    app.eqSpectrum(100, 0.016, pre, post);
    check(pre.size() == 100 && post.size() == 100, "eq: спектр на 100 столбцов");
    app.selectChannel(Channel::master);
    check(app.eqChannel() == EqSpec::masterChannel && a.core.spectra.channel.load() == EqSpec::masterChannel &&
          app.eqBand() == -1, "eq: MST — мастер");
    app.togglePatterns();
    check(!app.eqView() && a.core.spectra.channel.load() == -1, "eq: PATTERNS закрывает EQ");
}

void grid() {
    CoreAudio a;
    App app(&a);
    Project p;
    p.rows = 2;
    p.fillRows();
    p.tracks[1].clips[1]->scale = 1;
    p.tracks[1].clips[1]->root = 9;
    app.loadProject(p);
    app.tapCell(1, 1);
    check(app.project().tracks[1].slot == 1 && !app.project().tracks[1].stopped && app.selectedTrack() == 1,
          "grid: тап без игры — клип сразу");
    check(app.project().scale == 1 && app.project().root == 9, "grid: лидер — гамма и нота клипа");
    app.tapCell(1, 1);
    check(app.project().tracks[1].stopped, "grid: тап по играющей — стоп");
    app.queueScene(0);
    check(!app.project().tracks[0].stopped && app.project().tracks[1].slot == 0, "grid: строка — все дорожки");
    const auto w = app.gridWindow();
    check(w.rows[0] == 0 && w.rows[1] == 1 && w.plusRow == 2 && w.maxTop == 0, "grid: окно строк");
    app.addRow();
    check(app.project().visibleRows() == 3 && app.project().tracks[5].clipFilled(2), "grid: «+» — строка");
    app.copyClip(0, 0);
    app.pasteClip(3, 2);
    check(app.project().tracks[3].clipFilled(2) && app.canUndo(), "grid: copy / paste");
    app.togglePlay();
    a.run(3000);
    app.tick(1);
    app.tapCell(2, 1);
    a.run(256);
    app.tick(1.1);
    check(app.queued(2) == 1, "grid: при игре — ожидание");
    a.run(100000);
    app.tick(3);
    check(app.queued(2) == -2 && app.project().tracks[2].slot == 1, "grid: запуск с конца прохода");
    app.togglePlay();
}

void projects() {
    const std::string dir = (std::filesystem::temp_directory_path() / "jambo-app-projects").string();
    std::filesystem::remove_all(dir);
    {
        CoreAudio a;
        App app(&a);
        Project first;
        first.bpm = 101;
        app.openStorage(dir, first);
        check(app.project().bpm == 101 && app.isNewProject() && app.projectList().empty(), "app: первый запуск");
        app.tick(1);
        app.setBpm(133);
        app.tick(1.4);
        check(!ProjectStore(dir).load(ProjectStore::autosaveId), "app: автосохранение не сразу");
        app.tick(3);
        const auto au = ProjectStore(dir).load(ProjectStore::autosaveId);
        check(au && au->doc.bpm == 133, "app: Autosave через 1,5 с");
        app.saveProjectAs("Jam", "");
        check(!app.unsaved() && !app.isNewProject() && app.session().name == "Jam", "app: SAVE AS");
        app.setBpm(90);
        check(app.unsaved(), "app: unsaved");
        app.flushAutosave();
    }
    {
        CoreAudio a;
        App app(&a);
        app.openStorage(dir, Project());
        check(app.project().bpm == 90 && app.session().name == "Jam" && app.unsaved(), "app: запуск — Autosave");
        const std::string id = app.session().id;
        app.openProject(id);
        check(app.project().bpm == 133 && !app.unsaved(), "app: LOAD — как сохранено");
        app.newProject();
        check(app.session().name == "Project 1" && app.project().bpm == 120, "app: NEW");
        app.deleteProject(id);
        check(app.projectNamed("Jam").empty(), "app: удалён");
    }
    std::filesystem::remove_all(dir);
}

/// Перестановка дорожки во время игры: голоса переезжают с ней — звук без провала (прежде — тишина до шага).
void moveTrackKeepsSound() {
    CoreAudio a;
    App app(&a);
    Project p;
    p.tracks[0].setSynth(17);
    p.tracks[1].setSynth(18);
    app.loadProject(p);
    app.selectTrack(0);
    for (int s = 0; s < 16; s++) app.toggleStep(s);
    app.selectTrack(1);
    for (int s = 0; s < 16; s += 2) app.toggleStep(s);
    app.togglePlay();
    std::vector<float> l(256), r(256);
    auto rms = [&]() {
        a.core.render(256, l.data(), r.data());
        double e = 0;
        for (float x : l) e += double(x) * x;
        return std::sqrt(e / 256);
    };
    for (int k = 0; k < 400; k++) rms();
    app.moveTrack(0, 3);
    double low = 1;
    for (int k = 0; k < 20; k++) low = std::min(low, rms());
    check(low > 0.02 && app.project().tracks[3].sample == SynthKey::key(17), "app: перестановка на ходу — звук без провала");
}

void projectImport() {
    const auto dir = std::filesystem::temp_directory_path() / ("jambo-import-" + Library::newId());
    {
        CoreAudio audio;
        App app(&audio);
        app.openStorage((dir / "Projects").string(), Project{});
        app.saveProjectAs("Jam", "");
        const auto original = app.session().id;
        Project next;
        next.bpm = 147;
        const auto first = app.saveImportedProject({"Jam", next, 0});
        const auto second = app.saveImportedProject({"Jam", next, 0});
        check(first && second && first->id != second->id && first->name == "Jam (imported)" &&
                  second->name == "Jam (imported 2)", "app: imported projects are separate copies");
        check(app.session().id == original && app.project().bpm == 120, "app: import preserves current project");
        if (first) app.openProject(first->id);
        check(first && app.session().id == first->id && app.project().bpm == 147, "app: imported project loads normally");
    }
    std::filesystem::remove_all(dir);
}

void sliceAndWarpEditor() {
    const auto dir = std::filesystem::temp_directory_path() / ("jambo-warp-" + Library::newId());
    CoreAudio audio;
    App app(&audio);
    app.openLibraryStorage(dir.string());
    auto sample = std::make_shared<SampleData>(4800, true);
    for (int i = 0; i < sample->frames(); ++i) {
        sample->left()[i] = i < 3600 ? 0.25f : 0.75f;
        sample->right()[i] = sample->left()[i];
    }
    const auto key = SampleStore::store(*sample, app.sampleStore()->dir());
    Project project;
    project.bpm = 132;
    project.tracks[0].sample = key;
    project.files[key] = "Slices.wav";
    app.loadProject(project);

    app.editorSlice(-1, 0.25);
    app.editorSlice(-1, 0.5);
    check(app.editorSliceLabels() == std::vector<std::string>{"C3", "C#3"}, "slice editor: chromatic labels");
    app.setSamplerSettings(SliceMap::scale, true);
    check(app.editorSliceLabels() == std::vector<std::string>{"C3", "D3"}, "slice editor: scale labels");
    check(!app.editorAssignSlice(0, 50) && app.editorAssignSlice(1, 52), "slice editor: rejects a note already assigned");
    app.editorSliceRole(1, 0);
    app.editorAssignSlice(0, -1);
    check(app.editorSliceLabels() == std::vector<std::string>{"NO NOTE", "E3"} && app.editorSliceRoles()[1] == 0,
          "slice editor: note-free mark and RANDOM role are stored separately");
    const auto pieces = SliceRhythm::slicePieces(app.project().tracks[0], *sample, app.project(), app.sliceMap(), true);
    check(pieces.size() == 1 && pieces[0].note == 52 && pieces[0].role == 0,
          "slice editor: RANDOM consumes the assigned note and role");

    using W = App::WarpEdit;
    app.editWarp(W::enter, -1, 0, 0);
    check(app.editorWarpInfo().bpm == 132, "warp editor: opening in follow mode sets empty file BPM");
    app.editWarp(W::start, -1, 0, 0);
    app.editWarp(W::add, -1, 0.5, 0);
    app.editWarp(W::add, -1, 0.5001, 0);
    check(app.editorWarpInfo().marks.size() == 2, "warp editor: rejects markers closer than 1 ms");
    app.editWarp(W::move, 1, 0, 1.5);
    app.editWarp(W::place, 1, 0.75, 2);
    check(app.editorWarpInfo().marks[1] == WarpMark{0.75, 2}, "warp editor: top flag changes source frame and beat");
    app.undo();
    check(app.editorWarpInfo().marks[1] == WarpMark{0.5, 1.5}, "warp editor: undo restores the source and beat");
    app.redo();
    app.editWarp(W::bypass, -1, 0, 0);
    check(app.editorWarpInfo().isOff() && app.editorWarpInfo().marks.size() == 2, "warp editor: bypass keeps its markers");
    app.editWarp(W::bypass, -1, 0, 0);
    const auto cutRemap = app.editorEdit(3, 0.25, 0.5);
    check(!cutRemap, "editor: CUT resets the viewport");
    const auto cut = app.sampleData(app.editorKey());
    check(cut && cut->frames() == 3600 && app.editorWarpInfo().marks.size() == 2 &&
        std::abs(app.editorWarpInfo().marks.back().at - 2.0 / 3) < 1e-9, "warp editor: CUT remaps the warp marks");
    app.editWarp(W::automatic, -1, 0.25, 0);
    check(!app.editorWarpInfo().marks.empty() && app.editorWarpInfo().marks.front() == WarpMark{0, 0},
          "warp editor: AUTO WARP begins at the first beat");
    app.editWarp(W::clear, -1, 0, 0);
    check(app.editorWarpInfo().marks.empty() && app.editorWarpInfo().bpm == 132, "warp editor: CLEAR WARP preserves BPM");
    app.editorEdit(4, 0, 1);
    check(app.editorKey().empty(), "sample editor: CLEAR removes the track sample");
    app.undo();
    check(app.editorKey() != key && app.sampleData(app.editorKey()), "sample editor: CLEAR can be undone");
    std::filesystem::remove_all(dir);
}

void arrangerApp() {
    CoreAudio audio;
    App app(&audio);
    app.toggleStep(0);
    const int slot = app.project().tracks[0].slot;
    app.togglePatterns();
    app.toggleArrange();
    check(app.arrangeView() && !app.patternView(), "arrange app: PATTERNS and ARRANGE are mutually exclusive");
    app.addArrangeBlock(0, slot, 5, false);
    check(app.project().tracks[0].blocks.size() == 1 && app.project().tracks[0].blocks[0].start == 4 &&
          app.project().tracks[0].blocks[0].pattern.has_value(),
          "arrange app: add snaps and creates an independent clip");
    check(app.project().tracks[0].editBlock == 0, "arrange app: selected block is the edit target");
    app.setArrangeAuto(0, "filter", {{4, 0.2}, {8, 0.8}});
    const auto value = app.project().tracks[0].autoValue("filter", 6);
    check(value && std::abs(*value - 0.5) < 1e-9, "arrange app: AUTO curves are placed in timeline blocks");
    app.undo();
    check(!app.project().tracks[0].autoValue("filter", 6), "arrange app: UNDO removes the AUTO edit");
    app.setArrangeLoop(4, 12);
    app.toggleArrangeLoop();
    app.seekArrange(8);
    app.togglePlay();
    audio.run(64'000);
    app.tick(1.3);
    check(app.arrangePlaying() && app.arrangePosition() >= 4 && app.arrangePosition() < 12,
          "arrange app: playback follows the loop and reports its position");
    app.togglePatterns();
    check(app.patternView() && !app.arrangeView() && app.arrangePlaying() &&
          !app.project().tracks[0].editBlock,
          "arrange app: the timeline keeps playing while PATTERNS is open");
    app.togglePlay();
    app.togglePlay();
    check(!app.arrangePlaying(), "arrange app: PLAY in PATTERNS starts the grid");
    app.togglePlay();
    app.toggleArrange();
    app.addArrangeBlock(0, slot, 4, true, 20);
    const auto& blocks = app.project().tracks[0].blocks;
    check(blocks.size() >= 2 && blocks.front().start == 4 && blocks.back().start == 24 &&
          blocks[blocks.size() - 2].end() == 24,
          "arrange app: dropping on a junction inserts a block and shifts later blocks");
}

void arrangerSelectionTransfer() {
    CoreAudio audio;
    App app(&audio);
    app.toggleStep(0);
    app.selectTrack(1);
    app.toggleStep(0);
    const int row = app.project().tracks[0].slot;
    app.addArrangeBlock(0, row, 16, false);
    app.addArrangeSection(16);
    GridSelection selection;
    selection.cells[row] = {0, 1};
    const auto preview = app.previewGridSelection(selection, 16);
    check(preview.size() >= 4 && preview[0] == 1, "arrange selection: preview detects overlap");
    app.sendSelectionToArrange(selection, 16, 1);
    const auto& p = app.project();
    check(p.tracks[0].blocks.size() == 2 && p.tracks[0].blocks[0].start == 16 &&
          p.tracks[0].blocks[1].start == p.tracks[0].blocks[0].end() &&
          p.tracks[1].blocks.size() == 1 && p.arrange.markers[0].at == p.tracks[0].blocks[1].start,
          "arrange selection: INSERT shifts blocks and sections and puts selected clips in parallel");
    app.undo();
    check(app.project().tracks[0].blocks.size() == 1 && app.project().tracks[1].blocks.empty(),
          "arrange selection: transfer is one undo operation");
    app.sendSelectionToArrange(selection, 0, 2);
    check(!app.project().tracks[1].blocks.empty() && app.project().tracks[1].blocks[0].start >= 32,
          "arrange selection: TO END starts after the last block");
}

void songRender() {
    CoreAudio audio;
    App app(&audio);
    Project project;
    project.tracks[0].setSynth(16);  // KICK: пустая дорожка по умолчанию не звучит.
    app.loadProject(project);
    app.toggleStep(0);
    app.addArrangeBlock(0, app.project().tracks[0].slot, 0, false);
    app.setArrangeLoop(0, 4);
    app.toggleArrangeLoop();
    const auto source = app.beginSongExport();
    check(source && !app.beginSongExport(), "song export: prepares one snapshot at a time");
    if (!source) return;
    int frames = 0;
    float peak = 0;
    const auto sink = [&](const float* left, const float* right, int n) {
        frames += n;
        for (int i = 0; i < n; ++i) peak = std::max({peak, std::abs(left[i]), std::abs(right[i])});
        return true;
    };
    const int expected = int(std::llround(source->project.arrangeEnd() * 15.0 * 48000 / source->project.bpm));
    check(renderSong(*source, 0, sink, [] { return false; }, [](double) {}) && frames == expected && peak > 0.001,
          "song export: renders the audible arrangement once and cuts precisely at its end");
    frames = 0;
    check(renderSong(*source, 2, sink, [] { return false; }, [](double) {}) && frames == expected + 96000,
          "song export: a fixed tail has the requested duration");
    frames = 0;
    check(!renderSong(*source, -1, sink, [&] { return frames >= 2048; }, [](double) {}) && frames == 2048,
          "song export: cancellation stops the PCM stream");
    app.endSongExport();
    check(app.project().arrange.loop, "song export: the project loop is preserved");
}

void arrangerRecording() {
    CoreAudio audio;
    App app(&audio);
    app.toggleStep(0);
    app.togglePatterns();
    app.toggleArrangeRecord();
    check(app.arrangeRecordArmed() && !app.arrangeRecording(), "arrange record: arm waits for PLAY");
    app.togglePlay();
    audio.run(52'000);
    app.tick(1.1);
    check(app.arrangeRecording() && !app.arrangeRecordedSegments().empty(),
          "arrange record: the transport records segments from the grid");
    app.togglePlay();
    check(!app.arrangeRecording() && !app.project().tracks[0].blocks.empty(),
          "arrange record: STOP commits recorded clips to the timeline");
    app.undo();
    check(app.project().tracks[0].blocks.empty(), "arrange record: UNDO removes the take in one edit");
}

void arrangerReference() {
    const auto dir = std::filesystem::temp_directory_path() / ("jambo-ref-" + Library::newId());
    CoreAudio audio;
    App app(&audio);
    app.openLibraryStorage(dir.string());
    auto sample = std::make_shared<SampleData>(4800, false);
    std::fill_n(sample->left(), sample->frames(), 0.25f);
    const auto key = SampleStore::store(*sample, app.sampleStore()->dir());
    app.toggleArrange();
    app.toggleArrangeReference();
    check(app.libRefMode(), "arrange REF: without a reference, opens its library mode");
    app.libImported(app.libImportFolder(), {key, "Ref.wav", sample, 48});
    check(!app.browserOpen() && app.project().arrange.showRef && app.project().arrange.ref &&
          app.project().arrange.ref->key == key, "arrange REF: import opens the reference lane");
    check(!app.library().sameName(Library::root, "Ref.wav"), "arrange REF: direct import stays out of the library");
    app.toggleArrangeReferenceB();
    app.setMeterVol(Levels::master, -7);
    check(app.arrangeReferenceB() && app.project().arrange.ref && app.project().arrange.ref->gain == -7 &&
          app.project().outVol != -7, "arrange REF: B controls reference gain");
    app.togglePatterns();
    check(!app.arrangeReferenceB(), "arrange REF: outside ARRANGE the project is audible");
    std::filesystem::remove_all(dir);
}

void arrangerRecordAutomation() {
    CoreAudio audio;
    App app(&audio);
    app.selectTrack(1);
    app.toggleStep(0);
    const int clip = app.project().tracks[1].slot;
    app.toggleArrange();
    app.addArrangeBlock(1, clip, 0, false, 32);
    const auto base = app.knobStart(6);
    app.toggleArrangeAuto();
    app.toggleRec();
    app.togglePlay();
    audio.run(16'000);
    app.tick(0.34);
    app.knobFinger(6, true);
    app.beginGesture();
    app.setKnob(6, 0.2);
    audio.run(16'000);
    app.tick(0.67);
    app.setKnob(6, 0.8);
    app.knobFinger(6, false);
    app.endGesture();
    const auto points = app.project().tracks[1].autoPoints("filter");
    check(points.size() >= 2 && std::abs(points.front().y - 0.2) < 1e-9 &&
          std::abs(points.back().y - 0.8) < 1e-9, "arrange AUTO: records a gesture at transport positions");
    app.togglePlay();
    check(std::abs(app.knobStart(6) - base) < 1e-9, "arrange AUTO: restores the manual value after a take");
    app.undo();
    check(app.project().tracks[1].autoPoints("filter").empty(), "arrange AUTO: UNDO removes a recorded gesture");
}

void sampleEditor() {
    const auto dir = std::filesystem::temp_directory_path() / ("jambo-editor-" + Library::newId());
    {
        CoreAudio audio;
        App app(&audio);
        app.openLibraryStorage(dir.string());
        auto pcm = std::make_shared<SampleData>(480, true);
        for (int i = 0; i < pcm->frames(); ++i) {
            pcm->left()[i] = 0.25f;
            pcm->right()[i] = 0.5f;
        }
        const auto raw = SampleStore::store(*pcm, app.sampleStore()->dir());
        app.libImported(Library::root, {raw, "Editor test.wav", pcm, 48});
        app.openBrowser({});
        const auto* original = app.library().sameName(Library::root, "Editor test.wav");
        if (original) app.libTap(original->id);
        app.closeBrowser();
        check(!raw.empty() && app.editorKey() == raw, "editor: LOAD sets the selected track's sample");
        app.editorSlice(-1, 0.125);
        app.editorSlice(-1, 0.5);
        app.editorSlice(1, 0.625);
        const auto trim = app.editorEdit(0, 0.2503, 0.7498);
        const auto trimmed = app.editorKey();
        check(trim && trim->key == trimmed && trim->begin == 0.25 && trim->end == 0.75,
            "editor: viewport remap uses the stored frame boundaries");
        const auto trimData = app.sampleData(trimmed);
        const auto marks = app.project().tracks[0].sliceMarks(trimmed);
        check(trimData && trimData->frames() == 240 && trimmed != raw, "editor: TRIM saves a shorter copy");
        check(marks.size() == 1 && std::abs(marks[0].at - 0.75) < 1e-6, "editor: TRIM remaps the remaining slice");
        const auto monoRemap = app.editorEdit(2, 0, 1);
        check(monoRemap && monoRemap->key == app.editorKey() && monoRemap->begin == 0 && monoRemap->end == 1,
            "editor: same-length edits preserve the whole viewport coordinate range");
        const auto mono = app.sampleData(app.editorKey());
        check(mono && mono->channels() == 1 && std::abs(mono->left()[0] - 0.375f) < 1e-6, "editor: MONO averages stereo");
        app.editorEdit(1, 0, 1);
        const auto normalized = app.sampleData(app.editorKey());
        check(normalized && std::abs(normalized->left()[0] - std::pow(10.0, -0.1 / 20)) < 1e-6,
            "editor: NORMALIZE uses a -0.1 dBFS peak");
        const auto processed = app.editorKey();
        app.editorSave();
        app.libSave("Edited", "");
        app.closeBrowser();
        app.selectTrack(1);
        app.openBrowser({});
        const auto* saved = app.library().sameName(Library::root, "Edited");
        if (saved) app.libTap(saved->id);
        check(app.editorKey() == processed && app.project().tracks[1].sliceMarks(processed).size() == 1,
            "editor: SAVE and LOAD carry slice markers to another track");
        app.closeBrowser();
        Project slices = app.project();
        auto& sliced = slices.tracks[1];
        sliced.slices[processed] = {SliceMark(0.25, {}, {}, true), SliceMark(0.5, {}, {}, true)};
        app.loadProject(slices);
        app.selectTrack(1);
        app.setRandomSettings(4);
        check(app.randomButton(), "random: free slice markers expose RANDOM");
        app.chordTap();
        const Step* first = app.project().tracks[1].step(0);
        check(first && first->on && first->notes == std::vector<int>{Params::sampleRoot} &&
              first->locks.count("start") && first->locks.at("start") == 25 &&
              first->locks.count("end") && first->locks.at("end") == 50,
              "random: a free slice generates the right START and END locks");
        app.undo();
        check(app.project().tracks[1].pages() == slices.tracks[1].pages(), "random: slice generation is undoable");
        check(app.startSampleCapture(StemRing::master), "editor: REC starts from master");
        audio.run(1024);
        app.tick(1);
        app.selectTrack(2);
        app.stopSampleCapture();
        audio.run(512);
        app.tick(1.1);
        check(!app.sampleCapturing() && app.project().tracks[1].sample != processed,
            "editor: recording replaces the sample on the track selected at start");
        app.undo();
        check(app.project().tracks[1].sample == processed, "editor: recording is one undoable change");
    }
    std::filesystem::remove_all(dir);
}

void midi() {
    constexpr double interval = 60.0 / (123 * 24);
    ClockFollower follower;
    for (int i = 0; i < 60; ++i) follower.tick(10 + i * interval);
    check(std::abs(follower.tempo().value_or(0) - 123) < 1e-6, "midi: tempo from incoming 24 PPQN");
    follower.arm();
    check(follower.tick(10 + 60 * interval), "midi: Start waits for the first clock tick");
    check(std::abs(follower.stepTime(1).value_or(0) - (10 + 66 * interval)) < 1e-6,
        "midi: one step is six ticks");
    follower.tick(10 + 64 * interval);
    follower.tick(10 + 64 * interval + 0.0001);
    check(std::abs(follower.tempo().value_or(0) - 123) < 1e-5, "midi: burst does not move the estimated tempo");

    ClockPlan plan;
    auto free = plan.update(10, 120, std::nullopt, true, 0.1);
    check(free.events.size() == 5 && !free.flush && free.events.front().status == 0xF8,
        "midi: free-running output clocks while stopped");
    auto start = plan.update(10.02, 120, ClockPlan::Grid{0, 10.06, 0.125}, true, 0.1);
    check(start.flush && std::count_if(start.events.begin(), start.events.end(),
        [](const auto& e) { return e.status == 0xFA && std::abs(e.time - 10.06) < 1e-8; }) == 1,
        "midi: Start and realignment at the audible first step");
    const auto stop = plan.update(10.19, 120, std::nullopt, true, 0.1);
    check(!stop.events.empty() && stop.events.front().status == 0xFC, "midi: Stop is sent when playback stops");

    CoreAudio audio;
    App app(&audio);
    app.selectTrack(1);
    app.midiMappedKnob(4, 127, 0, 0.25, 0.75);
    check(std::abs(app.knobStart(4) - 0.75) < 1e-6, "midi: absolute CC uses the chosen knob range");
    app.midiMappedKnob(4, 127, 2, 0.25, 0.75);
    check(app.knobStart(4) < 0.75, "midi: 2's complement encoder supports negative steps");
    app.midiRealtime(0xFA, 20, true, 0);
    check(!app.playing(), "midi: application is armed before first tick");
    for (int i = 0; i < 8; ++i) app.midiRealtime(0xF8, 20 + i * interval, true, 0);
    check(app.playing() && std::abs(app.runningBpm() - 123) < 1e-6, "midi: application follows input tempo and transport");
    app.tickMidiSync(21, false, true, true, 0);
    check(app.runningBpm() == app.project().bpm, "midi: input timeout restores project tempo");
    app.midiRealtime(0xFC, 21, true, 0);
    check(!app.playing(), "midi: input Stop stops the application");
}

void midiTargets() {
    namespace mt = MidiTarget;
    CoreAudio audio;
    App app(&audio);
    auto down = [&](int target) { app.midiMappedButton(target, true, true, false); };
    auto up = [&](int target) { app.midiMappedButton(target, false, true, false); };
    app.midiMappedKnob(mt::bpm, 127, 0, 0, 0.5);
    check(app.project().bpm == 140, "midi targets: BPM uses the mapped range");
    app.midiMappedKnob(mt::level, 0, 0, 0.5, 1);
    check(app.meterVol(app.meterChannel()) == -30, "midi targets: LEVEL applies to the selected meter");
    down(mt::track + 1);
    check(app.selectedTrack() == 1, "midi targets: track selection");
    down(mt::mute + 1);
    check(app.project().tracks[1].mute, "midi targets: MUTE works outside MIX");
    down(mt::solo);
    check(app.project().tracks[0].solo, "midi targets: SOLO works outside MIX");
    down(mt::arm);
    check(app.stemLit(0), "midi targets: ARM toggles a track stem");
    down(mt::armMaster);
    check(app.stemLit(StemRing::master), "midi targets: ARM MST toggles the master stem");
    down(mt::chord);
    down(mt::chordType + 1);
    check(app.chordOn() && app.chordKind() == 1, "midi targets: CHORD toggle and type");
    const auto octave = app.octave();
    down(mt::octaveUp);
    check(app.octave() == octave + 1, "midi targets: octave arrows");
    down(mt::key);
    check(!app.litNotes().empty(), "midi targets: keyboard note down");
    up(mt::key);
    check(app.litNotes().empty(), "midi targets: keyboard note releases");
    down(mt::lfo);
    app.midiMappedKnob(4, 127, 0, 0, 1);
    const auto assign = app.assign(4);
    check(assign && assign->lfo == 1 && assign->amount == 1, "midi targets: held LFO sets modulation depth with a knob");
    up(mt::lfo);
    check(app.heldSources().empty(), "midi targets: source releases");
    down(mt::step);
    check(app.heldSteps().count(0) == 1, "midi targets: step is held for p-lock");
    up(mt::step);
    check(app.heldSteps().empty() && app.stepInfo(0, 0).on, "midi targets: a short step tap toggles on release");
    app.addPage();
    down(mt::page);
    down(mt::page + 1);
    check(app.shownSeq().playFrom() == 0 && app.shownSeq().playTo() == 1, "midi targets: two held pages select their range");
    up(mt::page);
    up(mt::page + 1);
    app.addRow();
    const auto playingClip = app.project().tracks[1].slot;
    const int clip = mt::clip + mt::windowRows + 1;
    down(clip);
    check(app.project().tracks[1].slot == playingClip, "midi targets: clip launcher respects its condition");
    app.midiMappedButton(clip, true, true, false, true);
    check(app.project().tracks[1].slot == 1, "midi targets: GLOBAL clip launches even with grid closed");
}

int runAppTests() {
    projectImport();
    midiTargets();
    midi();
    sampleEditor();
    arrangerApp();
    arrangerRecording();
    arrangerSelectionTransfer();
    songRender();
    arrangerReference();
    arrangerRecordAutomation();
    sliceAndWarpEditor();
    moveTrackKeepsSound();
    eq();
    grid();
    projects();
    chords();
    arpRecording();
    randomSwiftRules();
    randomPatterns();
    rec();
    meter();
    morphApp();
    fxPages();
    pages();
    interruptedScreenKeys();
    undoRedoPages();
    reorderPresets();
    mod();
    sound();
    steps();
    knobs();
    keysAndPlay();
    std::printf("app: %d / %d passed\n", checks - fails, checks);
    return fails;
}
