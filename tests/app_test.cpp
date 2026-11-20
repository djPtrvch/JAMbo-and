// Состояние приложения (app/App): правки шагов и ручек, UNDO, клавиатура, ход игры — через настоящее ядро.
#include "app/App.h"
#include "engine/AudioCore.h"
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
    void publish(const Project& p) override { core.publish(p, {}, {}, now); }
    bool popOut(SeqOut& e) override {
        auto x = core.sequencer.out.pop();
        if (!x) return false;
        e = *x;
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
    app.openSoundMenu();
    app.cleanSampler();
    check(app.project().tracks[0].sample.empty() && !app.soundMenu(), "app: SAMPLER — без звука");
}
}  // namespace

int runAppTests() {
    sound();
    steps();
    knobs();
    keysAndPlay();
    std::printf("app: %d / %d passed\n", checks - fails, checks);
    return fails;
}
