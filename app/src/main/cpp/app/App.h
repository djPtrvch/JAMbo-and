// Состояние приложения (порт части AppModel из UI/AppModel*.swift, ветка swift JAMbo): проект, UNDO, выбор дорожки
// и канала, зажатые шаги, правки шагов и ручек, клавиатура, транспорт, ход игры из очереди секвенсора. Экран (Kotlin)
// зовёт действия и читает данные для отрисовки; всё — в основном потоке (JNI — под мьютексом jni_bridge).
#pragma once
#include "engine/CoreEvent.h"
#include "EqDisplay.h"
#include "MeterState.h"
#include "MasterMeters.h"
#include "ScopeState.h"
#include "MixAnalysis.h"
#include "MixStereo.h"
#include "MasterWin.h"
#include "model/MasterPresets.h"
#include "engine/StemRing.h"
#include "master/MasterChain.h"
#include "icons/EngineIcons.h"
#include "synth/Wavetable.h"
#include "files/SampleStore.h"
#include "model/Library.h"
#include "sampler/Sampler.h"
#include "sampler/SoundSnapshot.h"
#include "model/History.h"
#include "model/KnobDefaults.h"
#include "model/Params.h"
#include "model/Project.h"
#include "model/ProjectSession.h"
#include "sequencer/PlayClock.h"
#include "sequencer/SeqOut.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace jambo {

/// Звук для App: события и снимки в ядро, сообщения секвенсора из него (AudioEngine; в тестах — AudioCore напрямую).
struct AppAudio {
    virtual ~AppAudio() = default;
    virtual void push(const CoreEvent& e) = 0;
    /// Снимки проекта: o — таблицы WTABLE, LEVEL RELATIVE, сэмпл предпрослушки (extra); samples — сэмплы по ключу.
    virtual void publish(const Project& p, const SoundSnapshot::Options& o,
                         const std::map<std::string, SampleRef>& samples) = 0;
    virtual bool popOut(SeqOut& e) = 0;
    /// Голоса сэмплера из колбэка (полосы воспроизведения окна сэмпла).
    virtual bool popVoice(VoiceOut&) { return false; }
    /// Цепочка мастера колбэка (телеметрия COMP, CLIP, LIM и скоп); nullptr — звука нет.
    virtual MasterChain* master() { return nullptr; }
    /// Кольцо записи стемов колбэка; кадров отрендерено (точка записи — мигание за 5 с до конца).
    virtual StemRing* stemRing() { return nullptr; }
    virtual long framesRendered() const { return 0; }
    /// Ход транспорта из колбэка (запись с REC).
    virtual PlayClock::State clock() const { return {}; }
    /// Время хоста (с, часы колбэка), чей звук слышен сейчас: сейчас минус задержка выхода.
    virtual double heardNow() const { return 0; }
    /// Выход источника модуляции для интерфейса: дорожка·4 + (LFO1–3, ENV).
    virtual double modOut(int) const { return 0; }
    /// Уровни колбэка (измеритель); nullptr — звука ещё нет.
    virtual Levels* levels() { return nullptr; }
    virtual const Levels* levels() const { return nullptr; }
    /// Фаза LFO (дорожка·3 + n) и ENV (дорожка·2: мс от запуска, гейт в мс) для миниатюр MOD.
    virtual double modPhase(int) const { return 0; }
    virtual double envOut(int i) const { return i % 2 == 0 ? -1 : 0; }
    /// Точки съёма спектров EQ (nullptr — звука нет) и сдвиг динамической полосы k канала ch, dB.
    virtual Spectra* spectra() { return nullptr; }
    virtual double eqDynOffset(int, int) const { return 0; }
};

/// Что выбрано в строке дорожек: дорожка, канал микшера MIX или мастер MST.
enum class Channel { track, mix, master };

/// Что показывает ручка (KnobState в AppModelEdits.swift; без LFO/ENV и кривых аранжировки — позже).
struct KnobState {
    const ParamDef* param = nullptr;  // nullptr — неактивная
    std::string label;
    double value = 0, norm = 0, baseNorm = 0;
    bool locked = false;
    bool col4 = false;
    /// Назначенный LFO/ENV; heldMatch — держат этот источник (на ручке — степень, видна зона воздействия).
    std::optional<ModAssign> assign;
    bool heldMatch = false;
    double srcLo = -1, srcHi = 1;
};

/// Что показать на шаге (StepInfo в SequencerRow.swift).
struct StepInfo {
    bool on = false, tail = false, playing = false, held = false, outside = false, last = false;
    bool manualLock = false, col4Lock = false, ownSound = false;
    std::string label;
};

/// Окно звука (левая колонка): заголовок, строки «подпись · значение», подпись стрелок папки.
struct SoundInfo {
    int engine = -1;  // −1 — сэмпл (или без звука)
    std::string title, detail, preset, folderLabel;
    std::vector<std::string> rows;
    int folderTotal = 0;
};

/// Миниатюра LFO/ENV (MOD): подпись, кривая (x 0…1; y: LFO −1…1, ENV 0…1), точка.
struct ModMini {
    std::string title;
    std::vector<double> xs, ys;
    double dotX = 0, dotY = 0;
};

/// Ячейка миниатюры страницы.
struct ThumbCell {
    bool on = false, tail = false, outside = false;
};

class App {
public:
    explicit App(AppAudio* audio);

    // Транспорт и проект
    const Project& project() const { return project_; }
    void loadProject(const Project& p);
    bool playing() const { return playing_; }
    bool metronome() const { return metronome_; }
    void togglePlay();
    void toggleMetronome();
    void setBpm(double v);
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void undo();
    void redo();
    /// Непрерывный жест (поворот ручки, протягивание): одна запись UNDO на жест.
    void beginGesture();
    void endGesture();

    // Дорожки
    int selectedTrack() const { return selected_; }
    Channel channel() const { return channel_; }
    void selectTrack(int i);
    void selectChannel(Channel c);
    /// Свайп по дорожке: вниз — mute, вверх — solo; включённый выключается любым свайпом. Без UNDO.
    void swipe(int i, bool down);

    // Шаги (секвенсор): индекс p·16 + s
    /// Страницы в секвенсоре: [first, last].
    std::pair<int, int> viewPages() const;
    StepInfo stepInfo(int page, int col) const;
    void holdStep(int index, bool on);
    bool stepUsed(int index) const { return used_.count(index) != 0; }
    const std::set<int>& heldSteps() const { return held_; }
    void toggleStep(int index);
    void setNoteLength(int index, int len);
    /// Зажат a, тап по b: b — копия a; true — скопировано (прежний b запомнен для restoreCopy).
    bool copyStep(int a, int b);
    /// Отменить последнюю копию в шаг index (двойной тап при зажатом первом шаге).
    void restoreCopy(int index);
    void setPatternLength(int lastIndex);

    // Ручки (слоты 0…15, ряд за рядом)
    KnobState knobState(int slot) const;
    double knobStart(int slot) const;
    void setKnob(int slot, double norm);
    void knobDoubleTap(int slot);

    // Страницы (правая колонка)
    enum class PageAction { left, right, up, down, noteUp, noteDown, duplicate, copy, paste, clear, del };
    int viewPage() const;
    /// Удержание миниатюры: открыть страницу в секвенсоре, не меняя играющих.
    void openPage(int p);
    void addPage();
    /// Тап (протягивание) по миниатюрам: эти страницы играют, первая открыта.
    void playPages(int a, int b);
    void movePage(int from, int to);
    void pageAction(PageAction a, int page);
    void pageArrow(PageAction a);
    bool canDuplicate(int page) const;
    bool canDelete(int page) const;
    bool hasPageClipboard() const { return pageClipboard_.has_value(); }
    /// Скорость открытого клипа (Clip::rates; 0 — обычная).
    int clipRate() const;
    void setClipRate(int r);
    std::vector<ThumbCell> thumb(int page) const;
    /// Миниатюра страницы page клипа k дорожки i (ячейка сетки паттернов).
    std::vector<ThumbCell> clipThumb(int i, int k, int page) const;
    const Track& shownSeq() const { return seq(); }

    // Звук дорожки (левая колонка)
    SoundInfo soundInfo() const;
    /// Значения иконки движка: ручки 1.1–1.4 и Decay 2.2 (0…1) с p-lock зажатого или играющего шага.
    IconValues iconValues(int engine) const;
    /// Элементы иконки (WTABLE — по таблице дорожки).
    EngineIcons::Elements iconElements(int engine, const IconValues& v);
    /// Меню звука открыто; шаги, для которых (пусто — для дорожки).
    const std::optional<std::vector<int>>& soundMenu() const { return soundMenu_; }
    /// Тап по окну звука: меню (зажаты шаги — для них; пальцы потом можно убрать).
    void openSoundMenu();
    void closeSoundMenu();
    /// Движок на дорожку или, с зажатыми шагами, — их свой звук.
    void pickSynth(int engine);
    /// SAMPLER: дорожка без звука.
    void cleanSampler();
    /// Меню звука шагов → TRACK: снова звук дорожки.
    void stepSoundToTrack();
    /// Стрелка: соседний звук папки; с зажатыми шагами — звук этих шагов.
    void folderArrow(int dir);

    // LFO1–3, ENV (1–4): назначение на ручки
    void setSource(int s, bool on, double now);
    const std::set<int>& heldSources() const { return heldSources_; }
    /// Источник для поворота ручки: держат источник, шаги не зажаты, ручки дорожки.
    std::optional<int> assignSource() const;
    void knobFinger(int slot, bool on);
    std::optional<ModAssign> assign(int slot) const;
    void setAssign(int slot, int lfo, double amount);
    void clearAssign(int slot, int lfo);
    /// Степень источника lfo на ручке (другой источник или нет — 0).
    double assignStart(int slot, int lfo) const;
    /// Положение ручки с модуляцией (0…1); nullopt — без назначения.
    std::optional<double> modulatedNorm(int slot) const;
    bool anyAssign() const;
    /// Источники, назначенные на ручки, которые держит палец (обводка их кнопок).
    std::set<int> fingerSources() const;

    // FX и MOD (удержание или toggle), SHIFT
    enum class GridPage { fx, mod };
    /// Ручки показывают FX / MOD: зажата кнопка на дорожке (из FX и MOD — нажатая последней).
    bool fxMode() const;
    bool modMode() const;
    bool pageOn(GridPage p) const { return p == GridPage::fx ? fxHeld_ : modHeld_; }
    bool latched(GridPage p) const { return p == GridPage::fx ? latchFx_ : latchMod_; }
    /// Режим toggle (настройки браузера — их хранит экран).
    void setLatched(bool fx, bool mod);
    /// Нажатие FX или MOD: SHIFT + нажатие — режим toggle кнопки (и обратно); в toggle — вкл./выкл.; иначе удержание.
    void pagePress(GridPage p);
    void pageRelease(GridPage p);
    bool shiftHeld() const { return shiftHeld_; }
    /// Миниатюра LFOn (1–3) или ENV (4) выбранной дорожки — пока держат MOD.
    ModMini modMini(int n) const;
    void setShift(bool on);
    /// SHIFT для ручек: пока держат SHIFT + MORPH, SHIFT занята удержанием — ручки крутятся как без неё.
    bool knobShift() const { return shiftHeld_ && !morphAllHeld_; }

    // MORPH
    bool morph() const { return morph_; }
    void toggleMorph();
    void morphPress();
    void morphRelease();
    /// Кнопка MORPH горит: при касании сразу видно, каким станет режим.
    bool morphShown() const;
    /// Пока держат SHIFT + MORPH (все дорожки) — без чёрной линии MODE TAP.
    bool morphAllHeld() const { return morphAllHeld_; }
    bool morphHoldMode() const { return morphHoldMode_; }
    /// SETTINGS → MORPH: RETURN (MorphReturn) и MODE HOLD (настройки — у экрана).
    void setMorphSettings(int ret, bool hold) { morphReturn_ = ret; morphHoldMode_ = hold; bump(); }
    int morphReturn() const { return morphReturn_; }
    /// SETTINGS → LEVEL: RELATIVE — регулятор справа подрезает громкость дорожки (trim) от громкости в MIX.
    void setLevelRelative(bool on);
    bool levelRelative() const { return levelRelative_; }
    bool quantize() const { return quantize_; }
    /// UNDO: с зажатой MORPH — сбросить изменения режима MORPH, иначе — обычная отмена.
    void undoTapped();
    /// SHIFT + тап по ручке: Dly Time и Speed LFO — по темпу / свободно, RATE, Filter — тип, ANALOG — модуляция,
    /// октава B, режим (undo — второй тап двойного: отменить первый); true — обработан.
    bool knobShiftTap(int slot, bool undo = false);

    // Измеритель справа
    int meterChannel() const;
    const MeterState& meters() const { return meters_; }
    float meterLufs() const;
    void resetMeterPeak();
    void resetLoudness();
    /// Итоговая громкость канала измерителя (подпись VOL и регулятор).
    double meterVol(int ch) const;
    void setMeterVol(int ch, double db);
    /// Палец на регуляторе: жест (одна запись UNDO) и подпись VOL.
    void setVolDrag(bool on);
    /// Стерео ли дорожка на метре (стерео где-то на дорожке — метр стерео).
    bool trackStereo(int t) const;

    // REC: запись нот при игре
    bool recOn() const { return recOn_; }
    void toggleRec();
    /// SETTINGS → RECORD (у экрана): QUANTIZE, OVERDUB.
    void setRecordSettings(bool quantize, bool overdub) { quantize_ = quantize; overdub_ = overdub; bump(); }

    // Гамма, нота, CHORD, меню клавиатуры (AppPiano.cpp)
    void setScale(int v);
    void setRoot(int v);
    /// Меню клавиатуры (настройки — у экрана): 1 или 2 октавы, VELOCITY.
    void setPianoSettings(int octaves, bool velocity);
    int pianoOctaves() const { return pianoOctaves_; }
    bool velocity() const { return velocity_; }
    bool overdub() const { return overdub_; }
    /// Режим аккордов клавиш выбранной дорожки и тип аккорда (у каждой дорожки свои, не в проекте).
    bool chordOn() const;
    int chordKind() const;
    void toggleChord();
    void setChordKind(int k);
    /// Подпись клавиши в режиме CHORD: «Em · i» и второй строкой нижняя нота («E3»).
    std::string chordKeyLabel(int k) const;
    /// Верхняя октава клавиатуры выбранной дорожки (ноты не выше 127).
    int topOctave() const;
    /// SWING дорожки (меню клавиатуры): значение без p-lock и LFO/ENV.
    double trackSwing() const;
    void setSwing(double v);

    // Клавиатура
    int octave() const;
    int keyCount() const;
    int keyNote(int k) const;
    std::optional<std::vector<int>> keyNotes(int k) const;
    /// Подсвечены (нажаты, играют, ноты зажатого шага); обведены (выбраны / ноты зажатого шага).
    std::set<int> litNotes() const;
    std::set<int> shownNotes() const;
    void keyDown(int id, int k, double velocity);
    void keyUp(int id);
    void setOctave(int o);
    /// Ноты с MIDI-входа — на выбранную дорожку (как клавиша; id — номер ноты со сдвигом).
    void midiNote(int note, int velocity);

    // EQ (AppEq.cpp)
    bool eqView() const { return eqView_; }
    void toggleEqView();
    /// Канал EQ: дорожка или EqSpec::masterChannel (MIX, MST).
    int eqChannel() const;
    Eq eqTarget() const;
    int eqBand() const { return eqBand_; }
    std::optional<EqBand> selectedBand() const;
    void selectEqBand(int k);
    /// Двойной тап по пустому месту: новая bell-полоса, она же выбрана.
    void addEqBand(double freq, double gain);
    void setEqBand(int k, std::optional<double> freq, std::optional<double> gain = std::nullopt,
                   std::optional<double> q = std::nullopt);
    void setEqType(int k, int type);
    void setEqSlope(int k, int slope);
    void setEqMs(int k, int ms);
    /// Источник динамической полосы: −1 OFF, 0 SELF, n — дорожка n.
    void setEqDyn(int k, int src);
    void setEqDynParams(int k, std::optional<double> thr, std::optional<double> range);
    void toggleEqBand(int k);
    void deleteEqBand(int k);
    void toggleEqBypass();
    /// Сдвиги динамических полос сейчас, dB.
    std::vector<double> eqDynOffsets() const;
    /// Подпись источника DYN: —, OFF, SELF, имя дорожки.
    std::string eqDynName(const EqBand& b) const;
    /// Спектры до и после EQ на n столбцов (доли высоты).
    void eqSpectrum(int n, double dt, std::vector<float>& pre, std::vector<float>& post);

    // Сетка паттернов (AppGrid.cpp)
    bool patternView() const { return patternView_; }
    void togglePatterns();
    void tapCell(int i, int k);
    /// Удержание: пустая — только выбор; заполненная — правка без запуска.
    void holdCell(int i, int k);
    /// Тап по строке: при игре — с начала такта (повторный — отмена), без игры — сразу.
    void queueScene(int k, bool atPass = false);
    void copyClip(int i, int k);
    void pasteClip(int i, int k);
    bool hasClipClipboard() const { return clipClipboard_.has_value(); }
    void deleteClip(int i, int k);
    /// Перетаскивание: независимая копия клипа в другую ячейку.
    void dropClip(int ai, int ak, int bi, int bk);
    void addRow();
    void duplicateRow(int k);
    void moveRow(int a, int b);
    void deleteRow(int k);
    void copyRow(int k);
    void pasteRow(int k);
    bool hasRowClipboard() const { return rowClipboard_.has_value(); }
    void setScene(int k, const std::string& name, int repeats);
    /// Меню сетки: START FROM ROW, AT THE END, + ADDS, ▲ ▼ ARROWS; LOOP ROW (переключение).
    enum class GridOption { start, endStop, plusCopy, arrowSwitch, rowLoop };
    void setGridOption(GridOption o, int v);
    /// Окно строк: 8 строк сетки, «+» — тоже строка окна; rows — строка проекта (−1 — «+» или пусто).
    struct GridWindow {
        std::vector<int> rows;
        int plusRow = -1, top = 0, maxTop = 0;
    };
    GridWindow gridWindow() const;
    /// ▲ (−1) / ▼ (+1): VIEW — окно на строку; SWITCH при игре — ещё и переход строки в конце прохода.
    void gridArrow(int dir);
    void setGridTop(int top);
    /// Ожидание у дорожки (Launcher::none, stop или ячейка); играющая и ожидающая строки.
    int queued(int t) const { return queued_[size_t(t)]; }
    int sceneRow() const { return sceneRow_; }
    int sceneQueued() const { return sceneQueued_; }
    /// Доля прохода играющего клипа k дорожки i; проход строки: номер и доля.
    double cellProgress(int i, int k) const;
    std::pair<int, double> scenePass() const;

    // Мастер (AppMaster.cpp): индикация COMP | CLIP | LIM, BYPASS модулей (SAT 0, COMP 1, CLIP 2, LIM 3).
    const MasterMeters& masterMeters() const { return masterMeters_; }
    bool masterBypass(int m) const;
    /// Включить и выключить bypass модуля (одна запись в UNDO).
    void toggleMasterBypass(int m);
    /// Кнопка мастера: с SHIFT — BYPASS; без — окно модуля (с окнами модулей).
    void masterButton(int m);
    /// Значение параметра мастера (риски THRESHOLD и CEILING).
    double masterValue(const std::string& id, double def) const;
    /// Скоп (при MIX и MST): точки съёма COMP, CLIP, LIM — тап (SAT всегда).
    const ScopeState& scope() const { return scope_; }
    void setScopeWidth(int w) { scope_.width = std::max(1, w); }
    const std::array<bool, 4>& scopeOn() const { return scopeOn_; }
    void toggleScope(int k);
    /// Вид полос MIX: 0 LEVEL, 1 PAN, 2 FREQ, 3 MASK (MixMode).
    int mixMode() const { return mixMode_; }
    void setMixMode(int m);
    const MixAnalysis& mixAnalysis() const { return mixAn_; }
    /// Стерео по полосам и история громкости мастера (при MIX справа); тап — сброс.
    const BandStereo& bandStereo() const { return bandStereo_; }
    const LoudHistory& loudHistory() const { return loudHist_; }
    void resetBandStereo() { bandStereo_.reset(); }
    void resetLoudHistory();
    const Levels* levelsNow() const { return audio_->levels(); }

    // Окна модулей мастера (AppMasterWin.cpp): SAT 0, COMP 1, CLIP 2, LIM 3 — на месте центра при MST.
    std::optional<int> masterWin() const { return channel_ == Channel::master ? masterWin_ : std::nullopt; }
    void openMasterWindow(int m);
    /// Тап по MIX или MST в строке дорожек: выбор; второй за 0,35 с — закрыть окно модуля мастера или окно микшера.
    void tapChannel(Channel c);
    /// Окно микшера при MIX (тап по обзору полос): фейдеры над дорожками.
    bool mixerOpen() const { return channel_ == Channel::mix && mixerOpen_; }
    void setMixerOpen(bool on);
    /// Сброс наибольшего пика дорожки (нижняя подпись фейдера окна микшера).
    void resetTrackPeak(int ch) { meters_.resetPeak(ch); bump(); }

    // Микшер (AppMix.cpp): M, S, ● у ручек громкости, меню дорожки, перестановка, запись стемов и мастера.
    void toggleMute(int i);
    void toggleSolo(int i);
    struct TrackMenu {
        int slot;
        bool add;
    };
    const std::optional<TrackMenu>& trackMenu() const { return trackMenu_; }
    void openTrackMenu(int slot);
    void closeTrackMenu();
    void addTrack();
    void copyTrack(int i);
    bool hasTrackClip() const { return trackClipboard_.has_value(); }
    void pasteTrack(int i);
    void deleteTrack(int i);
    void renameTrack(int i, const std::string& name);
    void moveTrack(int from, int to);
    /// ● (канал — дорожка или StemRing::master): готовность; во время записи — дописать до конца прохода.
    void stemTap(int ch);
    bool stemLit(int ch) const { return stemArmed_.count(ch) || stemRecording_.count(ch); }
    /// Точка записи: видна, мигает часто (до конца меньше 5 с).
    std::pair<bool, bool> stemDot(int ch) const;
    void closeMasterWindow();
    bool masterUnity() const { return masterUnity_; }
    void setUnity(bool on);
    /// Параметры модуля в строке окна (по порядку полей).
    static std::vector<const ParamDef*> masterFields(int m);
    double masterParam(const ParamDef& p) const { return masterValue(p.id, p.def); }
    /// Значение из поля или дисплея окна (в пределах, с шагом; внутри жеста — одна запись UNDO).
    void setMaster(const ParamDef& p, double v);
    void tapRatio();
    void tapSatType();
    void tapRelease(const ParamDef& p);
    /// AUTO MAKEUP: 4 с замера громкости до и после компрессора; тап при замере — отмена, по выставленному — назад.
    void tapMakeup();
    std::string makeupLabel(const std::string& plain) const;
    /// Пресеты модулей (MasterPresets.json рядом с библиотекой).
    const MasterPresetBook& masterPresets() const { return masterPresets_; }
    void saveMasterPreset(int m, const std::string& name, std::optional<int> over = std::nullopt);
    void loadMasterPreset(int m, int k);
    void renameMasterPreset(int m, int k, const std::string& name);
    void deleteMasterPreset(int m, int k);
    void moveMasterPreset(int m, int from, int to);
    /// SAVE DEFAULT: параметры модуля — свои значения по умолчанию (KnobDefaults.json).
    void saveMasterDefault(int m);
    bool masterDefaultFlash(int m) const { return defaultFlash_ == m && now_ < defaultFlashUntil_; }
    /// История телеметрии дисплея модуля m (COMP, CLIP, LIM) и спектры сатуратора.
    TeleHistory& teleHistory(int m) { return teleHist_[size_t(std::max(1, std::min(3, m)) - 1)]; }
    SatSpectrumState& satSpectrum() { return satSpec_; }
    MasterChain* masterChain() { return audio_->master(); }
    double nowSec() const { return now_; }

    // Библиотека (AppLibrary.cpp)
    /// Хранилище сэмплов (base/Samples) и дерево библиотеки (base/Library.json).
    void openLibraryStorage(const std::string& base);
    bool browserOpen() const { return browser_.has_value(); }
    /// Шаги, на которые грузится звук (меню звука шагов); пусто — дорожка.
    std::vector<int> browserSteps() const { return browser_ ? browser_->steps : std::vector<int>{}; }
    void openBrowser(const std::vector<int>& steps);
    /// SAVE в меню звука: браузер в режиме сохранения пресета синта дорожки.
    void openBrowserSave();
    bool libSaveMode() const { return browser_ && browser_->savePreset; }
    /// Имя по умолчанию в поле NAME: движок и номер.
    std::string libDefaultName() const;
    /// Узел с тем же именем в папке сохранения (вопрос OVERWRITE / SAVE AS) или nullptr.
    const LibNode* libSameName(const std::string& name) const;
    /// Свободное имя («Name 2») — подсказка для SAVE AS.
    std::string libFreeName(const std::string& name) const;
    /// SAVE HERE (over — перезаписать этот узел); затем браузер — в загрузке.
    void libSave(const std::string& name, const std::string& over);
    void closeBrowser();
    const Library& library() const { return library_; }
    const std::string& libFolder() const { return libFolder_; }
    std::vector<LibNode> libItems() const;
    std::string libPath() const;
    bool libAtRoot() const { return libFolder_ == Library::root; }
    const std::optional<std::set<std::string>>& libPicked() const { return libPicked_; }
    const std::string& libNote() const { return libNote_; }
    void setLibNote(const std::string& s);
    bool libIsCurrent(const LibNode& n) const;
    void libEnter(const std::string& id);
    void libUp();
    void libNewFolder(const std::string& name);
    void libRename(const std::string& id, const std::string& name);
    void libDelete(const std::string& id);
    void libTap(const std::string& id);
    void libHold(const std::string& id);
    void libPickAll();
    void libCancelPick();
    void libLoadPicked();
    void libPreview(const std::string& id);
    void stopLibraryPreview();
    /// IMPORT: папка, куда кладутся файлы (запоминается экраном на время декодирования).
    std::string libImportFolder() const;
    /// Декодированный сэмпл (SampleStore::decode в фоне) — в память и в папку.
    void libImported(const std::string& folder, const SampleStore::Loaded& l);
    /// .jambopreset — в папку; false — не пресет.
    bool libImportPreset(const std::string& folder, const std::string& name, const std::string& data);
    const SampleStore* sampleStore() const { return samples_.get(); }
    SampleRef sampleData(const std::string& key) const;
    /// Полосы воспроизведения сэмпла key дорожки track: доля длины и непрозрачность (линия ноты тает за 1,2 с).
    std::vector<std::pair<double, double>> playheads(int track, const std::string& key) const;
    int headCount() const { return int(heads_.size()); }
    /// Звук окна: пока зажат шаг со своим звуком — его звук.
    std::string shownSound() const;

    // Проекты (AppProjects.cpp)
    /// Папка проектов: открыть Autosave, иначе последний проект, иначе новый (при первом запуске — firstRun).
    void openStorage(const std::string& dir, const Project& firstRun);
    const ProjectSession& session() const { return session_; }
    const std::vector<ProjectInfo>& projectList() const { return projectList_; }
    bool unsaved() const;
    /// Проект ещё не сохраняли кнопкой SAVE (его нет в списке): SAVE спрашивает имя.
    bool isNewProject() const;
    void refreshProjects();
    void newProject();
    void saveProject();
    /// SAVE AS: replaceId — проект с тем же именем (после вопроса о перезаписи), пусто — новый.
    void saveProjectAs(const std::string& name, const std::string& replaceId);
    /// id проекта с таким именем (кроме текущего и Autosave), пусто — нет.
    std::string projectNamed(const std::string& name) const;
    void renameProject(const std::string& id, const std::string& name);
    void deleteProject(const std::string& id);
    /// LOAD: проект — в том виде, в каком он сохранён SAVE; Autosave — в проект, из которого он.
    void openProject(const std::string& id);
    /// Уход в фон: записать Autosave сразу.
    void flushAutosave();

    /// Основной поток, ~60 раз в секунду: сообщения колбэка → ход игры. now — секунды.
    void tick(double now);
    /// Номер состояния: меняется при любой правке и смене показа (экран перечитывает данные).
    uint64_t revision() const { return revision_; }
    /// Мигание дорожки (триг) — бит на дорожку.
    uint32_t flashMask() const { return flashMask_; }
    bool beatFlash() const { return beatFlash_; }
    bool beatAccent() const { return beatAccent_; }
    /// Ход открытого клипа показанной последовательности (−1 — не играет).
    int openPlayPos() const;

private:
    AppAudio* audio_;
    Project project_;
    History<Project> history_;
    int selected_ = 0;
    Channel channel_ = Channel::track;
    int viewPage_ = 0;
    bool playing_ = false, metronome_ = false;
    std::set<int> held_, used_;
    std::array<std::vector<int>, Params::maxTracks> selNotes_{};
    std::array<bool, Params::maxTracks> chordMode_{};
    std::array<int, Params::maxTracks> chordKind_{};
    int pianoOctaves_ = 1;
    bool velocity_ = false;
    std::map<int, std::vector<int>> pressedKeys_;
    struct KeyHold {
        int track;
        std::vector<int> notes;
        /// Записанный шаг (длина — при отпускании) и время нажатия (с).
        std::optional<int> recIndex;
        double t0 = 0;
    };
    std::map<int, KeyHold> keyHolds_;
    std::optional<std::pair<int, Step>> lastCopy_;
    uint64_t revision_ = 1;
    std::optional<std::vector<int>> soundMenu_;
    bool spanView_ = true;
    std::optional<std::vector<Page>> pageClipboard_;
    std::set<int> heldSources_, knobFingers_;
    int lastSource_ = 0;
    std::optional<std::pair<int, double>> lastSourceTap_;
    std::optional<int> heldSource() const;
    bool morph_ = false, morphHeld_ = false, morphUsed_ = false, morphOffOnRelease_ = false, morphAllHeld_ = false;
    bool morphHoldMode_ = false;
    int morphReturn_ = 0;
    struct MorphGlide {
        std::vector<MorphPoint> points;
        double start, seconds;
    };
    std::optional<MorphGlide> morphGlide_;
    /// Время последнего кадра (с) — начало плавного возврата.
    double now_ = 0;
    /// Правка режима MORPH: без UNDO — ни своей записи, ни в записи жеста.
    void editMorph(const std::function<void(Project&)>& change);
    void stepMorphGlide(double now);
    void stopMorphGlide();
    void undoMorphEdits();
    void beginMorphAllHold();
    void endMorphAllHold();
    /// Ручка в MORPH меняет параметр на всех дорожках — пока держат SHIFT + MORPH.
    bool morphAllNow() const { return morph_ && morphAllHeld_ && (morphHeld_ || shiftHeld_); }
    bool fxHeld_ = false, modHeld_ = false, latchFx_ = false, latchMod_ = false, shiftHeld_ = false;
    GridPage lastPage_ = GridPage::fx;
    /// Клип-лидер (гамма, нота и общие FX — его; запуск клипов — с сеткой паттернов).
    int lead_ = 0;
    /// Параметры, которые строятся по проекту (SC Source с именами дорожек, Speed LFO по темпу) — по слоту.
    mutable std::array<ParamDef, 16> dynParams_;
    void setPageHeld(GridPage p, bool on);
    const ParamDef& fxParam(int slot) const;
    const ParamDef& modParam(int slot) const;
    /// Значение ручки без p-lock: FX клипа, иначе ручка дорожки.
    double baseValue(const ParamDef& p) const;
    void setFx(const ParamDef& p, double v);
    /// Общие FX (Duck) — в клип лидера; гамма и нота — в активные клипы играющих (AppModel.writeLead).
    void writeLead(Project& p) const;
    bool delaySynced() const;
    bool lfoSynced(int l) const;
    int lfoSpeedIndex(int slot) const;
    bool rateStretch() const;
    int filterType() const;
    void filterShiftTap(bool undo);
    bool analogShiftTap(const std::string& id, bool undo);
    /// Двойной тап без шага: к значению по умолчанию (свои значения по умолчанию — позже).
    void resetKnob(int slot);
    /// Назначения ставятся на ручки дорожки и FX (не на MOD, MIX, MST).
    bool canAssign() const { return channel_ == Channel::track && !modMode(); }
    void sourceTapped(int s, double now);
    std::pair<double, double> sourceRange(const ModAssign& a) const;
    double modSource(int t, int lfo) const;
    /// Свои значения ручек по умолчанию (SHIFT + удержание — позже; пока пустые).
    KnobDefaults knobDefaults_;
    MeterState meters_;
    bool recOn_ = false, quantize_ = true, overdub_ = false;
    int recTake_ = -1;
    std::set<int> takeTracks_, takeWritten_;
    std::pair<int, int> lastRec_{-1, -1};
    /// Начало прохода клипа дорожки (тик) — из колбэка.
    std::array<int, Params::maxTracks> trackStart_{};
    std::optional<int> heardTick() const;
    std::optional<int> record(int ti, const std::vector<int>& notes, std::optional<double> velocity);
    /// Правка дубля: первая нота — новая запись UNDO, следующие (пока не было других действий) — в неё же.
    void writeTake(int ti, const std::function<void(Track&)>& change);
    void endRecTake();
    void recPassStep(int ti, int pos);
    /// SETTINGS → LEVEL: RELATIVE (подстройка trim) или ABSOLUTE (по умолчанию).
    bool levelRelative_ = false;
    void tickMeters(double now);
    /// Таблицы WTABLE по ключу (встроенные строятся при первой нужде; свои — с библиотекой).
    std::map<std::string, std::shared_ptr<WtTable>> tables_;
    /// Таблица по ключу; своя (wt:…) — nullptr.
    const WtTable* table(const std::string& key);
    /// Снимки в ядро: проект и нужные ему таблицы (AppModel.publishAudio).
    void publishAudio();

    // Ход игры (LiveState.swift): позиции шагов, клип дорожки, мигание.
    std::vector<int> playPos_, cell_;
    std::array<double, Params::maxTracks> flashUntil_{};
    uint32_t flashMask_ = 0;
    bool beatFlash_ = false, beatAccent_ = false;
    double beatUntil_ = 0;

    bool patternView_ = false;
    bool eqView_ = false;
    int eqBand_ = -1;
    SpectrumDisplay spectrum_;
    void eqEdit(const std::function<void(Eq&)>& f);
    /// Что слушают точки съёма спектров: канал EQ на экране.
    void syncSpectra();
    int gridTop_ = 0;
    std::optional<Clip> clipClipboard_;
    std::optional<Project::RowClip> rowClipboard_;
    std::array<int, Params::maxTracks> queued_ = [] {
        std::array<int, Params::maxTracks> a{};
        a.fill(-2);
        return a;
    }();
    int sceneRow_ = -1, sceneStart_ = 0, sceneLen_ = 16, sceneQueued_ = -1;
    std::array<int, Params::maxTracks> startOrder_{};
    int startSeq_ = 0;
    std::vector<int> pendingStarts_;
    void started(int t);
    void applyPendingLead();
    void leadStopped(int t);
    void applyLead(int i);
    void queueClip(int i, int k);
    /// Открыть клип k дорожки i для правки (без UNDO).
    void openClip(int i, int k);
    /// Стоп: ожидания и строки сброшены (LiveState.stopped).
    void liveStopped();
    std::unique_ptr<ProjectStore> store_;
    // Библиотека
    struct Browser {
        std::vector<int> steps;
        /// SAVE: пресет синта дорожки — в открытую папку.
        bool savePreset = false;
    };
    std::optional<Browser> browser_;
    std::unique_ptr<SampleStore> samples_;
    std::unique_ptr<LibraryStore> libStore_;
    Library library_;
    bool libraryLoaded_ = false;
    std::string libFolder_ = Library::root;
    std::optional<std::set<std::string>> libPicked_;
    std::string libNote_;
    std::optional<std::string> libPreviewKey_;
    bool previewOn_ = false;
    std::map<int, VoiceOut> heads_;
    MasterMeters masterMeters_;
    ScopeState scope_;
    MixAnalysis mixAn_;
    BandStereo bandStereo_;
    std::optional<int> masterWin_;
    std::optional<TrackMenu> trackMenu_;
    std::optional<Track> trackClipboard_;
    std::set<int> stemArmed_, stemRecording_;
    std::map<int, std::string> stemNames_;
    StemCollector stems_;
    void drainStems();
    void saveStem(const SampleRef& data, const std::string& base);
    bool mixerOpen_ = false;
    double mixTapAt_ = -1;
    bool masterUnity_ = false;
    MasterPresetBook masterPresets_;
    std::string masterPresetsPath_, knobDefaultsPath_;
    int makeupState_ = 0;
    std::optional<double> makeupManual_;
    double makeupApplied_ = 0, makeupDue_ = 0;
    int makeupRun_ = 0;
    std::map<std::string, double> releaseManual_;
    std::array<TeleHistory, 3> teleHist_;
    SatSpectrumState satSpec_;
    int defaultFlash_ = -1;
    double defaultFlashUntil_ = 0;
    void tickMakeup();
    void loadAppFiles();
    bool makeupLive() const;
    LoudHistory loudHist_;
    int mixMode_ = 0;
    void tickMixAnalysis();
    std::array<bool, 4> scopeOn_{true, false, false, false};
    void tickMasterMeters(double now);
    /// Голоса из колбэка → полосы воспроизведения.
    void drainVoices();
    /// Кадр ядра, который слышен сейчас (по времени хоста с поправкой на задержку выхода).
    double heardFrame() const;
    void ensureLibrary();
    void libEdit(const std::function<void(Library&)>& change);
    std::vector<std::string> projectSampleKeys() const;
    /// Сэмплы проекта, которых нет в памяти, — с диска.
    void ensureSamples();
    bool libUseSamples(const std::vector<std::pair<std::string, std::string>>& list);
    std::string trackLabel(int i) const;
    ProjectSession session_;
    std::vector<ProjectInfo> projectList_;
    double autosaveDue_ = 0;
    /// Проект изменился: автосохранение — через ~1,5 с после первого изменения.
    void projectChanged();
    void tickAutosave(double now);
    void autosave();
    void writeSession(bool rewrite, const std::function<void(ProjectSession&)>& f);
    std::optional<std::string> readCurrentId() const;
    void writeCurrentId() const;

    void bump() { ++revision_; }
    const Track& track() const { return project_.tracks[size_t(std::min(selected_, int(project_.tracks.size()) - 1))]; }
    /// Последовательность в секвенсоре: выбранная дорожка, при MIX и MST — их свои.
    const Track& seq() const;
    std::vector<int> trigNotes() const;
    void edit(const std::function<void(Project&)>& change);
    /// Есть блок аранжировки без своей копии клипа.
    static bool needsOwnPatterns(const Project& p);
    void editTrack(const std::function<void(Track&)>& change);
    void editSeq(const std::function<void(Track&)>& change);
    void mutatePlay(const std::function<void(Project&)>& change, bool publish);
    void restore(const Project& snap);
    void changed();
    void inputNotes();
    std::set<int> litNotesPressed() const;
    void liveNoteOn(int t, const std::vector<int>& notes, int id, double velocity);
    void playEvent(const SeqOut& e);
    const ParamDef* param(int slot) const;
    std::optional<double> playLock(const std::string& id) const;
    int rawTrackPlayPos() const;
    std::vector<ParamDef> mixDefs_;
    std::optional<int> firstHeld() const;
    /// Значение ручки для окна синта: p-lock зажатого шага, при игре — играющего, иначе ручка дорожки.
    double synthValue(const ParamDef& p) const;
    std::string sampleName(const std::string& key) const;
};

}  // namespace jambo
