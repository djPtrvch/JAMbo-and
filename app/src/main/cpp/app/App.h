// Состояние приложения (порт части AppModel из UI/AppModel*.swift, ветка swift JAMbo): проект, UNDO, выбор дорожки
// и канала, зажатые шаги, правки шагов и ручек, клавиатура, транспорт, ход игры из очереди секвенсора. Экран (Kotlin)
// зовёт действия и читает данные для отрисовки; всё — в основном потоке (JNI — под мьютексом jni_bridge).
#pragma once
#include "engine/CoreEvent.h"
#include "icons/EngineIcons.h"
#include "synth/Wavetable.h"
#include "model/History.h"
#include "model/KnobDefaults.h"
#include "model/Params.h"
#include "model/Project.h"
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
    /// Снимки проекта; tables — таблицы WTABLE дорожек по ключу.
    virtual void publish(const Project& p, const std::map<std::string, std::shared_ptr<WtTable>>& tables) = 0;
    virtual bool popOut(SeqOut& e) = 0;
    /// Выход источника модуляции для интерфейса: дорожка·4 + (LFO1–3, ENV).
    virtual double modOut(int) const { return 0; }
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
    std::map<int, std::vector<int>> pressedKeys_;
    struct KeyHold {
        int track;
        std::vector<int> notes;
    };
    std::map<int, KeyHold> keyHolds_;
    std::optional<std::pair<int, Step>> lastCopy_;
    uint64_t revision_ = 1;
    std::optional<std::vector<int>> soundMenu_;
    std::set<int> heldSources_, knobFingers_;
    int lastSource_ = 0;
    std::optional<std::pair<int, double>> lastSourceTap_;
    std::optional<int> heldSource() const;
    /// Назначения ставятся на ручки дорожки (FX — позже; не на MOD, MIX, MST).
    bool canAssign() const { return channel_ == Channel::track; }
    void sourceTapped(int s, double now);
    std::pair<double, double> sourceRange(const ModAssign& a) const;
    double modSource(int t, int lfo) const;
    /// Свои значения ручек по умолчанию (SHIFT + удержание — позже; пока пустые).
    KnobDefaults knobDefaults_;
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
    /// Звук окна: пока зажат шаг со своим звуком — его звук.
    std::string shownSound() const;
    /// Значение ручки для окна синта: p-lock зажатого шага, при игре — играющего, иначе ручка дорожки.
    double synthValue(const ParamDef& p) const;
    std::string sampleName(const std::string& key) const;
};

}  // namespace jambo
