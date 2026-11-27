// JNI экрана: app.jambo.android.AppNative. «Выполни действие» — act(code, a, b, x) (коды — Act в AppNative.kt),
// «дай данные» — массивы по разделам. Вызовы из UI- и MIDI-потоков — под одним мьютексом.
#include <jni.h>
#include <codecvt>
#include <locale>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include "app/App.h"
#include "audio_bridge.h"
#include "audio_engine.h"
#include "jni_common.h"
#include "model/Scales.h"
#include "model/SynthEngines.h"

namespace {
std::mutex gLock;
jambo::App* gApp = nullptr;

/// Проект при запуске (Android, временно — пока нет меню звука и проектов): дорожка 1 — KICK, 2 — ANALOG, 3 — FM.
jambo::Project starterProject() {
    jambo::Project p;
    p.tracks[0].setSynth(16);
    p.tracks[1].setSynth(17);
    p.tracks[2].setSynth(18);
    return p;
}

jambo::App& app() {
    if (!gApp) {
        gApp = new jambo::App(audioEngine());
        gApp->loadProject(starterProject());
    }
    return *gApp;
}

enum Act {
    togglePlay = 1, toggleMetronome, undo, redo, setBpm, beginGesture, endGesture,
    selectTrack = 20, selectChannel, swipe,
    holdStep = 40, toggleStep, setNoteLength, copyStep, restoreCopy, setPatternLength, stepUsed,
    knobStart = 60, setKnob, knobDoubleTap,
    keyDown = 80, keyUp, octaveStep, midiNote, releaseScreenKeys,
    setSource = 85, knobFinger, assignSource, assignStart, setAssign,
    openSoundMenu = 90, closeSoundMenu, pickSynth, cleanSampler, stepSoundToTrack, folderArrow,
    tick = 100,
    pagePress = 120, pageRelease, setShift, knobShiftTap, setLatched, morphPress, morphRelease,
    resetMeterPeak = 130, resetLoudness, setMeterVol, volDrag, toggleRec,
    openPage = 110, addPage, playPages, movePage, pageAction, pageArrow, setClipRate,
    setScale = 140, setRoot, toggleChord, setChordKind, setPianoSettings, setRecordSettings, setSwing,
    flushAutosave = 150, refreshProjects, setMorphSettings, setLevelRelative,
    togglePatterns = 160, tapCell, holdCell, queueScene, copyClip, pasteClip, deleteClip, dropClip, addRow,
    duplicateRow, moveRow, deleteRow, copyRow, pasteRow, setGridOption, gridArrow, setGridTop,
    toggleEqView = 180, selectEqBand, setEqType, setEqSlope, setEqMs, setEqDyn, toggleEqBand, deleteEqBand,
    toggleEqBypass,
    setArpMode = 200, setArpRate, setArpOctaves, setArpLatch, setArpGate, setRandomSettings, chooseRandomPart,
};

jintArray ints(JNIEnv* env, const std::vector<jint>& v) {
    jintArray a = env->NewIntArray(jsize(v.size()));
    env->SetIntArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

// В проекте и .jambo — обычный UTF-8; NewStringUTF ждёт Modified UTF-8.
jstring javaString(JNIEnv* env, const std::string& value) {
    using Utf = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>;
    try {
        const auto utf16 = Utf().from_bytes(value);
        const std::vector<jchar> chars(utf16.begin(), utf16.end());
        return env->NewString(chars.data(), jsize(chars.size()));
    } catch (const std::range_error&) {
        // Старые локальные строки могли уже быть сохранены в Modified UTF-8.
        return env->NewStringUTF(value.c_str());
    }
}

jobjectArray strings(JNIEnv* env, const std::vector<std::string>& v) {
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray a = env->NewObjectArray(jsize(v.size()), cls, nullptr);
    for (size_t i = 0; i < v.size(); ++i) {
        jstring s = javaString(env, v[i]);
        env->SetObjectArrayElement(a, jsize(i), s);
        env->DeleteLocalRef(s);
    }
    return a;
}

const char* const kRoots[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}  // namespace

std::mutex& jniLock() { return gLock; }
jambo::App& jniApp() { return app(); }
jintArray jniInts(JNIEnv* env, const std::vector<jint>& v) { return ints(env, v); }
jobjectArray jniStrings(JNIEnv* env, const std::vector<std::string>& v) { return strings(env, v); }
std::string jniString(JNIEnv* env, jstring s) {
    if (!s) return {};
    const jchar* chars = env->GetStringChars(s, nullptr);
    if (!chars) return {};
    const std::u16string text(chars, chars + env->GetStringLength(s));
    env->ReleaseStringChars(s, chars);
    using Utf = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>;
    try {
        return Utf().to_bytes(text);
    } catch (const std::range_error&) {
        return {};
    }
}

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

FN(jdouble, act)(JNIEnv*, jobject, jint code, jint a, jint b, jdouble x) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    switch (code) {
    case togglePlay: m.togglePlay(); break;
    case toggleMetronome: m.toggleMetronome(); break;
    case undo: m.undoTapped(); break;
    case redo: m.redo(); break;
    case setBpm: m.setBpm(x); break;
    case beginGesture: m.beginGesture(); break;
    case endGesture: m.endGesture(); break;
    case selectTrack: m.selectTrack(a); break;
    case selectChannel: m.tapChannel(jambo::Channel(a)); break;
    case swipe: m.swipe(a, b != 0); break;
    case holdStep: m.holdStep(a, b != 0); break;
    case toggleStep: m.toggleStep(a); break;
    case setNoteLength: m.setNoteLength(a, b); break;
    case copyStep: return m.copyStep(a, b) ? 1 : 0;
    case restoreCopy: m.restoreCopy(a); break;
    case setPatternLength: m.setPatternLength(a); break;
    case stepUsed: return m.stepUsed(a) ? 1 : 0;
    case knobStart: return m.knobStart(a);
    case setKnob: m.setKnob(a, x); break;
    case knobDoubleTap:
        // Держат LFO/ENV — снять назначение этого источника; иначе — p-lock или значение по умолчанию.
        if (auto src = m.assignSource()) m.clearAssign(a, *src);
        else m.knobDoubleTap(a);
        break;
    case setSource: m.setSource(a, b != 0, x); break;
    case knobFinger: m.knobFinger(a, b != 0); break;
    case assignSource: return m.assignSource().value_or(0);
    case assignStart: return m.assignStart(a, b);
    case setAssign: m.setAssign(a, b, x); break;
    case keyDown: m.keyDown(a, b, x); break;
    case keyUp: m.keyUp(a); break;
    case releaseScreenKeys: m.releaseScreenKeys(); break;
    case octaveStep: m.setOctave(m.octave() + a); break;
    case midiNote: m.midiNote(a, b); break;
    case openSoundMenu: m.openSoundMenu(); break;
    case closeSoundMenu: m.closeSoundMenu(); break;
    case pickSynth: m.pickSynth(a); break;
    case cleanSampler: m.cleanSampler(); break;
    case stepSoundToTrack: m.stepSoundToTrack(); break;
    case folderArrow: m.folderArrow(a); break;
    case tick: {
        static double lastLatency = 0;
        if (x - lastLatency > 1) {
            lastLatency = x;
            audioEngine()->updateLatency();
        }
        m.tick(x);
        break;
    }
    case pagePress: m.pagePress(jambo::App::GridPage(a)); break;
    case pageRelease: m.pageRelease(jambo::App::GridPage(a)); break;
    case setShift: m.setShift(a != 0); break;
    case knobShiftTap: return m.knobShiftTap(a, b != 0) ? 1 : 0;
    case setLatched: m.setLatched(a != 0, b != 0); break;
    case morphPress: m.morphPress(); break;
    case morphRelease: m.morphRelease(); break;
    case resetMeterPeak: m.resetMeterPeak(); break;
    case resetLoudness: m.resetLoudness(); break;
    case setMeterVol: m.setMeterVol(m.meterChannel(), x); break;
    case volDrag: m.setVolDrag(a != 0); break;
    case toggleRec: m.toggleRec(); break;
    case openPage: m.openPage(a); break;
    case addPage: m.addPage(); break;
    case playPages: m.playPages(a, b); break;
    case movePage: m.movePage(a, b); break;
    case pageAction: m.pageAction(jambo::App::PageAction(a), b); break;
    case pageArrow: m.pageArrow(jambo::App::PageAction(a)); break;
    case setClipRate: m.setClipRate(a); break;
    case setScale: m.setScale(a); break;
    case setRoot: m.setRoot(a); break;
    case toggleChord: m.chordTap(); break;
    case setChordKind: m.chooseChordType(a); break;
    case setArpMode: m.setArpMode(a != 0); break;
    case setArpRate: m.setArpRate(a); break;
    case setArpOctaves: m.setArpOctaves(a); break;
    case setArpLatch: m.setArpLatch(a != 0); break;
    case setArpGate: m.setArpGate(x); break;
    case setRandomSettings: m.setRandomSettings(a); break;
    case chooseRandomPart: m.chooseRandomPart(a); break;
    case setPianoSettings: m.setPianoSettings(a, b != 0); break;
    case setRecordSettings: m.setRecordSettings(a != 0, b != 0); break;
    case setSwing: m.setSwing(x); break;
    case flushAutosave: m.flushAutosave(); break;
    case refreshProjects: m.refreshProjects(); break;
    case setMorphSettings: m.setMorphSettings(a, b != 0); break;
    case setLevelRelative: m.setLevelRelative(a != 0); break;
    case togglePatterns: m.togglePatterns(); break;
    case tapCell: m.tapCell(a, b); break;
    case holdCell: m.holdCell(a, b); break;
    case queueScene: m.queueScene(a); break;
    case copyClip: m.copyClip(a, b); break;
    case pasteClip: m.pasteClip(a, b); break;
    case deleteClip: m.deleteClip(a, b); break;
    case dropClip: m.dropClip(a, b, int(x) / 100, int(x) % 100); break;
    case addRow: m.addRow(); break;
    case duplicateRow: m.duplicateRow(a); break;
    case moveRow: m.moveRow(a, b); break;
    case deleteRow: m.deleteRow(a); break;
    case copyRow: m.copyRow(a); break;
    case pasteRow: m.pasteRow(a); break;
    case setGridOption: m.setGridOption(jambo::App::GridOption(a), b); break;
    case gridArrow: m.gridArrow(a); break;
    case setGridTop: m.setGridTop(a); break;
    case toggleEqView: m.toggleEqView(); break;
    case selectEqBand: m.selectEqBand(a); break;
    case setEqType: m.setEqType(a, b); break;
    case setEqSlope: m.setEqSlope(a, b); break;
    case setEqMs: m.setEqMs(a, b); break;
    case setEqDyn: m.setEqDyn(a, b); break;
    case toggleEqBand: m.toggleEqBand(a); break;
    case deleteEqBand: m.deleteEqBand(a); break;
    case toggleEqBypass: m.toggleEqBypass(); break;
    default: break;
    }
    return 0;
}

/// Папка проектов (filesDir/Projects): открыть Autosave, иначе последний проект; первый запуск — проект для старта.
FN(void, openStorage)(JNIEnv* env, jobject, jstring dir) {
    std::lock_guard<std::mutex> g(gLock);
    const char* c = env->GetStringUTFChars(dir, nullptr);
    const std::string d = c ? c : "";
    if (c) env->ReleaseStringUTFChars(dir, c);
    app().openStorage(d, starterProject());
}

/// Сетка паттернов: [открыта, дорожек, верх окна, макс. верх, строка «+» окна (−1), строк в окне, видимых строк,
/// игра, есть копия клипа, есть копия строки, строка старта, AT THE END STOP, + ADDS COPY, ARROWS SWITCH, LOOP ROW,
/// играющая строка, ожидающая, можно добавить строку]; затем по строке окна: строка проекта (−1), по дорожке —
/// ячейка: [биты заполнена 1, играет 2, открыта 4, ожидает 8; страниц n; по странице: триги | хвосты << 16,
/// за длиной]; затем по строке окна — повторы.
FN(jintArray, grid)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto& p = m.project();
    const auto w = m.gridWindow();
    const int n = int(p.tracks.size());
    std::vector<jint> v{m.patternView(), n, w.top, w.maxTop, w.plusRow, int(w.rows.size()), p.visibleRows(),
                        m.playing(), m.hasClipClipboard(), m.hasRowClipboard(), std::min(p.arrStart, p.visibleRows() - 1),
                        p.arrEndStop, p.plusCopy, p.arrowSwitch, p.rowLoop, m.sceneRow(), m.sceneQueued(), p.canAddRow()};
    for (int k : w.rows) {
        v.push_back(k);
        for (int i = 0; i < n; ++i) {
            const auto& t = p.tracks[size_t(i)];
            const bool filled = k >= 0 && t.clipFilled(k);
            const int q = m.queued(i);
            int bits = filled ? 1 : 0;
            if (filled && t.slot == k && !t.stopped) bits |= 2;
            if (m.channel() == jambo::Channel::track && i == m.selectedTrack() && k == t.open) bits |= 4;
            if (k >= 0 && (q == k || (t.slot == k && q == -1))) bits |= 8;
            v.push_back(bits);
            const auto r = filled ? t.clipRange(k) : std::nullopt;
            v.push_back(r ? r->upper - r->lower + 1 : 0);
            if (!r) continue;
            for (int pg = r->lower; pg <= r->upper; ++pg) {
                int on = 0, out = 0;
                const auto cells = m.clipThumb(i, k, pg);
                for (size_t s = 0; s < cells.size(); ++s) {
                    if (cells[s].on) on |= 1 << s;
                    if (cells[s].tail) on |= 1 << (s + 16);
                    if (cells[s].outside) out |= 1 << s;
                }
                v.push_back(on);
                v.push_back(out);
            }
        }
    }
    for (int k : w.rows) v.push_back(k >= 0 ? p.scenes[size_t(k)].repeats : 0);
    return ints(env, v);
}

/// Имена строк проекта (все видимые; без имени — номер) и имя выбранной в окне строки — по запросу.
FN(jobjectArray, sceneNames)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& p = app().project();
    std::vector<std::string> v;
    for (int k = 0; k < p.visibleRows(); ++k) v.push_back(p.sceneName(k));
    for (int k = 0; k < p.visibleRows(); ++k) v.push_back(p.scenes[size_t(k)].name);
    return strings(env, v);
}

/// Ход сетки (каждый кадр, пока открыта): [проход строки, доля·1000, по дорожке доля играющей ячейки·1000].
FN(jintArray, gridLive)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto& p = m.project();
    const auto sp = m.scenePass();
    std::vector<jint> v{sp.first, jint(sp.second * 1000)};
    for (int i = 0; i < int(p.tracks.size()); ++i)
        v.push_back(jint(m.cellProgress(i, p.tracks[size_t(i)].slot) * 1000));
    return ints(env, v);
}

FN(void, setScene)(JNIEnv* env, jobject, jint k, jstring name, jint repeats) {
    std::lock_guard<std::mutex> g(gLock);
    app().setScene(k, jniString(env, name), repeats);
}

namespace {
std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
/// Подписи EQ (EqFormat в AppModelEq.swift).
std::string eqHz(double f) {
    if (f < 1000) return fmt(f < 100 ? "%.1f Hz" : "%.0f Hz", f);
    return fmt(f < 10000 ? "%.2f kHz" : "%.1f kHz", f / 1000);
}
std::string eqGain(double g) { return (g > 0 ? "+" : "") + fmt("%.1f dB", g); }
const char* const kEqTypes[] = {"Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch"};
const char* const kEqMs[] = {"STEREO", "MID", "SIDE"};
}  // namespace

/// EQ: [открыт, мастер, EQ включён, выбранная полоса, дорожек], затем по месту полосы (10): [есть, вкл., тип,
/// крутизна, M/S, источник DYN].
FN(jintArray, eq)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto e = m.eqTarget();
    std::vector<jint> v{m.eqView(), m.eqChannel() == jambo::EqSpec::masterChannel, e.on, m.eqBand(),
                        jint(m.project().tracks.size())};
    for (int k = 0; k < jambo::EqSpec::bands; ++k) {
        const auto b = e.band(k);
        v.insert(v.end(), {b ? 1 : 0, b && b->on, b ? b->type : 0, b ? b->slope : 0, b ? b->ms : 0, b ? b->dyn : -1});
    }
    return ints(env, v);
}

/// Значения полос: по месту — [частота, усиление, Q, порог, сдвиг].
FN(jfloatArray, eqBands)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    const auto e = app().eqTarget();
    std::vector<jfloat> v;
    for (int k = 0; k < jambo::EqSpec::bands; ++k) {
        const auto b = e.band(k).value_or(jambo::EqBand());
        v.insert(v.end(), {jfloat(b.freq), jfloat(b.gain), jfloat(b.q), jfloat(b.thr), jfloat(b.range)});
    }
    jfloatArray a = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

/// Подписи строки EQ: [имя канала, TYPE, FREQ, GAIN, Q, M/S, DYN] выбранной полосы (нет — «—»).
FN(jobjectArray, eqTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const std::string name = m.channel() == jambo::Channel::track ? m.project().tracks[size_t(m.selectedTrack())].name
                                                                  : std::string("MASTER");
    const auto b = m.selectedBand();
    if (!b) return strings(env, {name, "—", "—", "—", "—", "—", "—"});
    std::string type = kEqTypes[b->type];
    if (jambo::EqSpec::isCut(b->type)) type += " " + std::to_string(jambo::EqSpec::slopeDb(b->slope));
    return strings(env, {name, type, eqHz(b->freq), jambo::EqSpec::hasGain(b->type) ? eqGain(b->gain) : "—",
                         fmt("%.2f", b->q), kEqMs[b->ms], m.eqDynName(*b)});
}

/// Правка полосы k: NaN — не менять.
FN(void, eqSetBand)(JNIEnv*, jobject, jint k, jdouble freq, jdouble gain, jdouble q) {
    std::lock_guard<std::mutex> g(gLock);
    auto o = [](double v) { return std::isnan(v) ? std::nullopt : std::optional<double>(v); };
    app().setEqBand(k, o(freq), o(gain), o(q));
}

FN(void, eqSetDyn)(JNIEnv*, jobject, jint k, jdouble thr, jdouble range) {
    std::lock_guard<std::mutex> g(gLock);
    auto o = [](double v) { return std::isnan(v) ? std::nullopt : std::optional<double>(v); };
    app().setEqDynParams(k, o(thr), o(range));
}

FN(void, eqAddBand)(JNIEnv*, jobject, jdouble freq, jdouble gain) {
    std::lock_guard<std::mutex> g(gLock);
    app().addEqBand(freq, gain);
}

/// Кривые EQ на n точек (dB): [биты стерео 1 | MID 2 | SIDE 4, стерео n, середина n, бока n, сдвиги DYN 10].
FN(jfloatArray, eqCurves)(JNIEnv* env, jobject, jint n) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto offs = m.eqDynOffsets();
    const auto c = jambo::eqCurves(m.eqTarget(), n, offs);
    std::vector<jfloat> v{jfloat((c.hasStereo ? 1 : 0) | (c.hasMid ? 2 : 0) | (c.hasSide ? 4 : 0))};
    for (double x : c.stereo) v.push_back(jfloat(x));
    for (double x : c.mid) v.push_back(jfloat(x));
    for (double x : c.side) v.push_back(jfloat(x));
    for (double x : offs) v.push_back(jfloat(x));
    jfloatArray a = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

/// Спектры до и после EQ на n столбцов (доли высоты, 0 — верх): [до n, после n].
FN(jfloatArray, eqSpectrum)(JNIEnv* env, jobject, jint n, jdouble dt) {
    std::lock_guard<std::mutex> g(gLock);
    std::vector<float> pre, post;
    app().eqSpectrum(n, dt, pre, post);
    pre.insert(pre.end(), post.begin(), post.end());
    jfloatArray a = env->NewFloatArray(jsize(pre.size()));
    env->SetFloatArrayRegion(a, 0, jsize(pre.size()), pre.data());
    return a;
}

/// SETTINGS: [QUANTIZE, OVERDUB, MORPH RETURN, MORPH MODE HOLD, LEVEL RELATIVE, флаги RANDOM].
FN(jintArray, settings)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {m.quantize(), m.overdub(), m.morphReturn(), m.morphHoldMode(), m.levelRelative(), m.randomSettings()});
}

FN(jobjectArray, morphReturnNames)(JNIEnv* env, jobject) { return strings(env, jambo::MorphReturn::names); }

/// Меню PROJECTS: [не сохранено, новый (SAVE спрашивает имя), проектов в списке].
FN(jintArray, projects)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {m.unsaved(), m.isNewProject(), jint(m.projectList().size())});
}

/// Строки списка по 6: id, подпись (у Autosave — «Autosave · проект»), время (с от 1970), темп, тональность
/// («C Min»), признаки (a — Autosave, c — текущий). Затем имя текущего проекта.
FN(jobjectArray, projectRows)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<std::string> v;
    for (const auto& i : m.projectList()) {
        const bool au = i.id == jambo::ProjectStore::autosaveId;
        v.push_back(i.id);
        v.push_back(au ? std::string(jambo::ProjectSession::autosaveName) + " · " + i.projectName.value_or("") : i.name);
        v.push_back(std::to_string(std::llround(i.updated)));
        v.push_back(std::to_string(std::lround(i.bpm)));
        v.push_back(jambo::ProjectSession::keyName(i));
        v.push_back(au ? "a" : (i.id == m.session().id ? "c" : ""));
    }
    v.push_back(m.session().name);
    return strings(env, v);
}

/// Действия PROJECTS со строками: 0 NEW, 1 SAVE, 2 SAVE AS (text — имя, id — перезаписать), 3 RENAME (id, text),
/// 4 удалить (id), 5 открыть LOAD (id), 6 занятое имя (text) → id, 7 случайное имя.
FN(jstring, projectAct)(JNIEnv* env, jobject, jint code, jstring id, jstring text) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const std::string a = jniString(env, id), t = jniString(env, text);
    std::string r;
    switch (code) {
    case 0: m.newProject(); break;
    case 1: m.saveProject(); break;
    case 2: m.saveProjectAs(t, a); break;
    case 3: m.renameProject(a, t); break;
    case 4: m.deleteProject(a); break;
    case 5: m.openProject(a); break;
    case 6: r = m.projectNamed(t); break;
    case 7: r = jambo::ProjectSession::randomName(m.projectList()); break;
    default: break;
    }
    return javaString(env, r);
}

/// Кадр (каждый кадр экрана): [ревизия (младшие 31 бит), ход открытого клипа, маска мигания, мигание доли,
/// акцент доли, игра, голосов сэмплера].
FN(jintArray, frame)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {jint(m.revision() & 0x7fffffff), m.openPlayPos(), jint(m.flashMask()), m.beatFlash() ? 1 : 0,
                      m.beatAccent() ? 1 : 0, m.playing() ? 1 : 0, m.headCount()});
}

/// Верхняя панель: [BPM·10, UNDO, REDO, игра, метроном, REC].
FN(jintArray, top)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {jint(std::lround(m.runningBpm() * 10)), m.canUndo(), m.canRedo(), m.playing(), m.metronome(), m.recOn()});
}

/// Подписи верхней панели: [гамма, корневая нота].
FN(jobjectArray, topTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& p = app().project();
    return strings(env, {jambo::Scales::all[size_t(p.scale)].name, kRoots[jambo::Scales::wrap(p.root, 12)]});
}

/// Дорожки: [число, выбранная, канал (0 дорожка, 1 MIX, 2 MST), по дорожке: биты mute 1, solo 2, MORPH 4].
FN(jintArray, tracks)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<jint> v{jint(m.project().tracks.size()), m.selectedTrack(), jint(m.channel())};
    for (const auto& t : m.project().tracks) v.push_back((t.mute ? 1 : 0) | (t.solo ? 2 : 0) | (t.morphEdited ? 4 : 0));
    return ints(env, v);
}

FN(jobjectArray, trackNames)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    std::vector<std::string> v;
    for (const auto& t : app().project().tracks) v.push_back(t.name);
    return strings(env, v);
}

/// Шаги показанных страниц: [первая, последняя, по шагу: биты on 1, tail 2, held 4, outside 8, last 16,
/// manualLock 32, col4Lock 64, ownSound 128] (играющий — по ходу из frame).
FN(jintArray, steps)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto [a, b] = m.viewPages();
    std::vector<jint> v{a, b};
    for (int p = a; p <= b; ++p)
        for (int c = 0; c < jambo::Params::stepsPerPage; ++c) {
            const auto s = m.stepInfo(p, c);
            v.push_back((s.on ? 1 : 0) | (s.tail ? 2 : 0) | (s.held ? 4 : 0) | (s.outside ? 8 : 0) | (s.last ? 16 : 0) |
                        (s.manualLock ? 32 : 0) | (s.col4Lock ? 64 : 0) | (s.ownSound ? 128 : 0));
        }
    return ints(env, v);
}

FN(jobjectArray, stepLabels)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto [a, b] = m.viewPages();
    std::vector<std::string> v;
    for (int p = a; p <= b; ++p)
        for (int c = 0; c < jambo::Params::stepsPerPage; ++c) v.push_back(m.stepInfo(p, c).label);
    return strings(env, v);
}

/// Ручки: по 16 слотам [активна, положение, база, p-lock, от центра, 4-я строка, источник назначения (0 — нет),
/// степень, ход источника от, до, держат этот источник] (float).
FN(jfloatArray, knobs)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<jfloat> v;
    for (int k = 0; k < 16; ++k) {
        const auto s = m.knobState(k);
        v.insert(v.end(), {s.param ? 1.f : 0.f, float(s.norm), float(s.baseNorm), s.locked ? 1.f : 0.f,
                           s.param && s.param->bipolar ? 1.f : 0.f, s.col4 ? 1.f : 0.f,
                           s.assign ? float(s.assign->lfo) : 0.f, s.assign ? float(s.assign->amount) : 0.f,
                           float(s.srcLo), float(s.srcHi), s.heldMatch ? 1.f : 0.f});
    }
    jfloatArray a = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

/// Подписи ручек: по слоту [подпись, значение].
FN(jobjectArray, knobTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<std::string> v;
    for (int k = 0; k < 16; ++k) {
        const auto s = m.knobState(k);
        v.push_back(s.label);
        if (s.assign && s.heldMatch) {
            // Две строки: источник (LFO1–3, ENV) и степень.
            const int pct = int(std::lround(s.assign->amount * 100));
            v.push_back(std::string(s.assign->lfo == 4 ? "ENV" : "LFO" + std::to_string(s.assign->lfo)) + "\n" +
                        (pct > 0 ? "+" : "") + std::to_string(pct) + "%");
        } else {
            v.push_back(s.param && s.param->format ? s.param->format(s.value) : "");
        }
    }
    return strings(env, v);
}

/// Клавиатура: [число клавиш, число клавиш двух октав (сторона клавиши), по клавише: нота, биты подсвечена 1,
/// обведена 2, корневая 4].
FN(jintArray, keys)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto lit = m.litNotes();
    const auto shown = m.shownNotes();
    const int n = m.keyCount();
    std::vector<jint> v{n, jambo::Scales::keyCount(m.project().scale, 2)};
    for (int k = 0; k < n; ++k) {
        const int note = m.keyNote(k);
        v.push_back(note);
        v.push_back((lit.count(note) ? 1 : 0) | (shown.count(note) ? 2 : 0) |
                    (jambo::Scales::wrap(note, 12) == m.project().root ? 4 : 0));
    }
    return ints(env, v);
}

FN(jobjectArray, keyLabels)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<std::string> v;
    for (int k = 0; k < m.keyCount(); ++k)
        v.push_back(m.chordOn() ? m.chordKeyLabel(k) : jambo::Params::noteName(m.keyNote(k)));
    return strings(env, v);
}

/// Клавиатура и CHORD: [CHORD вкл., тип аккорда, октав (1, 2), VELOCITY, OVERDUB, гамма, корневая нота, SWING·10,
/// SWING от·10, до·10, клавиш одной октавы (по ним — колонки экрана)].
FN(jintArray, piano)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {m.chordOn(), m.chordKind(), m.pianoOctaves(), m.velocity(), m.overdub(), m.project().scale,
                      m.project().root, jint(std::lround(m.trackSwing() * 10)),
                      jint(std::lround(jambo::Params::swing().min * 10)), jint(std::lround(jambo::Params::swing().max * 10)),
                      jambo::Scales::keyCount(m.project().scale, 1)});
}

/// Режим CHORD/ARP и настройки выбранной дорожки.
FN(jintArray, arp)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto& a = m.arp();
    return ints(env, {a.mode, a.on, a.dir, a.rate, a.octaves, a.latch, m.channel() == jambo::Channel::track,
                      m.chordStyle(), m.randomButton(), jint(std::lround(a.gate * 100)), m.randomPart()});
}

/// Подписи меню клавиатуры: [тип аккорда, SWING].
FN(jobjectArray, pianoTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return strings(env, {jambo::Scales::chordKinds[size_t(m.chordKind())], jambo::Params::swing().format(m.trackSwing())});
}

/// Имена гамм, типов аккорда (окно CHORD) и нот (меню ROOT).
FN(jobjectArray, scaleNames)(JNIEnv* env, jobject) {
    std::vector<std::string> v;
    for (const auto& sc : jambo::Scales::all) v.push_back(sc.name);
    return strings(env, v);
}

FN(jobjectArray, chordKindNames)(JNIEnv* env, jobject) { return strings(env, jambo::Scales::chordKinds); }

FN(jobjectArray, rootNames)(JNIEnv* env, jobject) { return strings(env, std::vector<std::string>(kRoots, kRoots + 12)); }

/// Окно и меню звука: [движок окна (−1 — сэмпл), звуков в папке, меню открыто, движок дорожки, сэмпл на дорожке,
/// число шагов меню, шаги меню…].
FN(jintArray, sound)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto info = m.soundInfo();
    const auto& menu = m.soundMenu();
    const std::string& sample = m.project().tracks[size_t(m.selectedTrack())].sample;
    const int te = jambo::SynthKey::engine(sample);
    std::vector<jint> v{info.engine, info.folderTotal, menu ? 1 : 0, te, !sample.empty() && te < 0 ? 1 : 0,
                        menu ? jint(menu->size()) : 0};
    if (menu) v.insert(v.end(), menu->begin(), menu->end());
    return ints(env, v);
}

/// Подписи окна звука: [заголовок, после тире, пресет, подпись стрелок, строки…].
FN(jobjectArray, soundTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    const auto info = app().soundInfo();
    std::vector<std::string> v{info.title, info.detail, info.preset, info.folderLabel};
    v.insert(v.end(), info.rows.begin(), info.rows.end());
    return strings(env, v);
}

/// Колонки меню звука: [число DRUMS, номера…, число SYNTH, номера…].
FN(jintArray, engines)(JNIEnv* env, jobject) {
    std::vector<jint> v{jint(jambo::SynthEngines::drums.size())};
    v.insert(v.end(), jambo::SynthEngines::drums.begin(), jambo::SynthEngines::drums.end());
    v.push_back(jint(jambo::SynthEngines::synths.size()));
    v.insert(v.end(), jambo::SynthEngines::synths.begin(), jambo::SynthEngines::synths.end());
    return ints(env, v);
}

/// Короткие имена движков по номеру (0…count−1; не занятый номер — пусто).
FN(jobjectArray, engineNames)(JNIEnv* env, jobject) {
    std::vector<std::string> v;
    for (int e = 0; e < jambo::SynthEngines::count(); ++e) {
        const auto* se = jambo::SynthEngines::engine(e);
        v.push_back(se ? se->name : "");
    }
    return strings(env, v);
}

/// Значения иконки окна звука: [движок (−1 — нет), h, t, m, q, d, am, ao].
FN(jfloatArray, iconValues)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const int e = m.soundInfo().engine;
    const auto v = m.iconValues(e);
    const jfloat a[8] = {jfloat(e), jfloat(v.h), jfloat(v.t), jfloat(v.m), jfloat(v.q), jfloat(v.d), jfloat(v.am),
                         v.ao ? 1.f : 0.f};
    jfloatArray r = env->NewFloatArray(8);
    env->SetFloatArrayRegion(r, 0, 8, a);
    return r;
}

/// Элементы иконки (поле 64×64) подряд: [вид, цвет (0 основной, 1 серый), прозрачность, заливка, пунктир, …]:
/// 0 прямоугольники — контур, n, по 4 числа; 1 волна — n, x0, x1, верх n, низ n; 2 контур — n, по (команда, x, y);
/// 3 текст — x, y, размер, длина, коды символов; 4 окружность — cx, cy, r.
FN(jfloatArray, icon)(JNIEnv* env, jobject, jint engine, jfloat h, jfloat t, jfloat mm, jfloat q, jfloat d, jint am,
                      jboolean ao) {
    std::lock_guard<std::mutex> g(gLock);
    jambo::IconValues v;
    v.h = h, v.t = t, v.m = mm, v.q = q, v.d = d, v.am = am, v.ao = ao;
    const auto els = app().iconElements(engine, v);
    std::vector<jfloat> o;
    for (const auto& e : els) {
        const auto head = [&](int kind) {
            o.insert(o.end(), {jfloat(kind), e.ink == jambo::IconInk::secondary ? 1.f : 0.f, jfloat(e.opacity.value_or(1)),
                               e.fill ? 1.f : 0.f, e.dash ? 1.f : 0.f});
        };
        if (e.text) {
            head(3);
            // UTF-8 → коды символов (× — два байта).
            std::vector<jfloat> cp;
            const std::string& s = *e.text;
            for (size_t i = 0; i < s.size();) {
                const unsigned char c = (unsigned char)s[i];
                if (c < 0x80) { cp.push_back(c); i += 1; }
                else if ((c >> 5) == 6 && i + 1 < s.size()) { cp.push_back(((c & 0x1f) << 6) | (s[i + 1] & 0x3f)); i += 2; }
                else if ((c >> 4) == 14 && i + 2 < s.size()) {
                    cp.push_back(((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f));
                    i += 3;
                } else i += 1;
            }
            o.insert(o.end(), {jfloat(e.textX), jfloat(e.textY), jfloat(e.textSize), jfloat(cp.size())});
            o.insert(o.end(), cp.begin(), cp.end());
        } else if (!e.wave.empty()) {
            head(1);
            const size_t n = std::min(e.wave.size(), e.waveBot.size());
            o.insert(o.end(), {jfloat(n), jfloat(e.waveX0), jfloat(e.waveX1)});
            for (size_t i = 0; i < n; ++i) o.push_back(jfloat(e.wave[i]));
            for (size_t i = 0; i < n; ++i) o.push_back(jfloat(e.waveBot[i]));
        } else if (!e.rects.empty()) {
            head(0);
            o.insert(o.end(), {e.contour ? 1.f : 0.f, jfloat(e.rects.size())});
            for (const auto& r : e.rects) o.insert(o.end(), {jfloat(r.x), jfloat(r.y), jfloat(r.w), jfloat(r.h)});
        } else if (e.circle) {
            head(4);
            o.insert(o.end(), {jfloat(std::get<0>(*e.circle)), jfloat(std::get<1>(*e.circle)), jfloat(std::get<2>(*e.circle))});
        } else {
            head(2);
            o.push_back(jfloat(e.cmds.size()));
            for (const auto& c : e.cmds) o.insert(o.end(), {jfloat(c.kind), jfloat(c.x), jfloat(c.y)});
        }
    }
    jfloatArray r = env->NewFloatArray(jsize(o.size()));
    env->SetFloatArrayRegion(r, 0, jsize(o.size()), o.data());
    return r;
}

/// Источники: [зажатые (бит 1 << n), обведённые — назначены на ручки под пальцем, есть назначения на ручках].
FN(jintArray, mod)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    jint held = 0, finger = 0;
    for (int s : m.heldSources()) held |= 1 << s;
    for (int s : m.fingerSources()) finger |= 1 << s;
    return ints(env, {held, finger, m.anyAssign() ? 1 : 0});
}

/// Кадр модуляции: по 16 ручкам положение с модуляцией (−1 — без назначения).
FN(jfloatArray, modNorms)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    jfloat v[16];
    for (int k = 0; k < 16; ++k) v[k] = float(m.modulatedNorm(k).value_or(-1));
    jfloatArray r = env->NewFloatArray(16);
    env->SetFloatArrayRegion(r, 0, 16, v);
    return r;
}

/// Значения ручек с модуляцией (пусто — без назначения или держат его источник).
FN(jobjectArray, modTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<std::string> v;
    for (int k = 0; k < 16; ++k) {
        const auto st = m.knobState(k);
        const auto n = m.modulatedNorm(k);
        v.push_back(n && st.param && st.param->format && !st.heldMatch ? st.param->format(st.param->fromNorm(*n)) : "");
    }
    return strings(env, v);
}

/// Миниатюры страниц: [число страниц, играют от, до, открыты от, до, скорость клипа, есть копия страниц,
/// по странице: можно duplicate, 16 ячеек — биты триг 1, хвост 2, за длиной 4].
FN(jintArray, pages)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const auto& t = m.shownSeq();
    const int n = int(t.pages().size());
    const auto [a, b] = m.viewPages();
    std::vector<jint> v{n, t.playFrom(), t.playTo(), a, b, m.clipRate(), m.hasPageClipboard() ? 1 : 0};
    for (int p = 0; p < n; ++p) {
        v.push_back(m.canDuplicate(p) ? 1 : 0);
        for (const auto& c : m.thumb(p)) v.push_back((c.on ? 1 : 0) | (c.tail ? 2 : 0) | (c.outside ? 4 : 0));
    }
    return ints(env, v);
}

/// Скорости клипа (меню страницы, SPEED): [значения…].
FN(jintArray, clipRates)(JNIEnv* env, jobject) {
    const auto& r = jambo::Clip::rates();
    return ints(env, std::vector<jint>(r.begin(), r.end()));
}

FN(jobjectArray, clipRateNames)(JNIEnv* env, jobject) { return strings(env, jambo::Clip::rateNames()); }

/// Кнопки правой колонки: [FX зажата, MOD зажата, FX toggle, MOD toggle, SHIFT, ручки FX, ручки MOD, SHIFT для ручек,
/// MORPH горит, у MORPH чёрная линия MODE TAP].
FN(jintArray, side)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    using P = jambo::App::GridPage;
    return ints(env, {m.pageOn(P::fx), m.pageOn(P::mod), m.latched(P::fx), m.latched(P::mod), m.shiftHeld(), m.fxMode(),
                      m.modMode(), m.knobShift(), m.morphShown(), !m.morphHoldMode() && !m.morphAllHeld()});
}

/// Миниатюры MOD: по LFO1–3 и ENV [точек n, x n, y n, точка x, y] подряд.
FN(jfloatArray, modMinis)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<jfloat> o;
    for (int n = 1; n <= 4; ++n) {
        const auto mm = m.modMini(n);
        o.push_back(jfloat(mm.xs.size()));
        for (double x : mm.xs) o.push_back(jfloat(x));
        for (double y : mm.ys) o.push_back(jfloat(y));
        o.push_back(jfloat(mm.dotX));
        o.push_back(jfloat(mm.dotY));
    }
    jfloatArray r = env->NewFloatArray(jsize(o.size()));
    env->SetFloatArrayRegion(r, 0, jsize(o.size()), o.data());
    return r;
}

FN(jobjectArray, modMiniTitles)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<std::string> v;
    for (int n = 1; n <= 4; ++n) v.push_back(m.modMini(n).title);
    return strings(env, v);
}

/// Измеритель справа (каждый кадр): [канал, стерео, заглушён, полос n, по полосе: уровень, холд, клиппинг, LUFS,
/// наибольший пик, громкость (dB), палец на регуляторе].
FN(jfloatArray, meter)(JNIEnv* env, jobject, jdouble now) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    const int ch = m.meterChannel();
    const bool track = ch < jambo::Levels::master;
    const bool stereo = !track || m.trackStereo(ch);
    const bool muted = track && ch < int(m.project().tracks.size()) && !m.project().audible(ch);
    const auto bars = m.meters().bars(ch, stereo);
    std::vector<jfloat> o{jfloat(ch), stereo ? 1.f : 0.f, muted ? 1.f : 0.f, jfloat(bars.size())};
    for (const auto& b : bars) o.insert(o.end(), {b.level, b.hold});
    o.insert(o.end(), {m.meters().clip(ch, now) ? 1.f : 0.f, m.meterLufs(), m.meters().maxPeak[size_t(ch)],
                       jfloat(m.meterVol(ch)), m.meters().volDrag ? 1.f : 0.f});
    jfloatArray r = env->NewFloatArray(jsize(o.size()));
    env->SetFloatArrayRegion(r, 0, jsize(o.size()), o.data());
    return r;
}
