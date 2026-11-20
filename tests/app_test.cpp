// Состояние приложения (app/App): правки шагов и ручек, UNDO, клавиатура, ход игры — через настоящее ядро.
#include "app/App.h"
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
    void publish(const Project& p, const std::map<std::string, std::shared_ptr<WtTable>>& tables) override {
        SoundSnapshot::Options o;
        o.tables = tables;
        core.publish(p, {}, o, now);
    }
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
}  // namespace

int runAppTests() {
    fxPages();
    pages();
    mod();
    sound();
    steps();
    knobs();
    keysAndPlay();
    std::printf("app: %d / %d passed\n", checks - fails, checks);
    return fails;
}
