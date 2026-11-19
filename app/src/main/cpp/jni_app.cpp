// JNI экрана: app.jambo.android.AppNative. «Выполни действие» — act(code, a, b, x) (коды — Act в AppNative.kt),
// «дай данные» — массивы по разделам. Вызовы из UI- и MIDI-потоков — под одним мьютексом.
#include <jni.h>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>
#include "app/App.h"
#include "audio_bridge.h"
#include "audio_engine.h"
#include "model/Scales.h"

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
    keyDown = 80, keyUp, octaveStep, midiNote,
    tick = 100,
};

jintArray ints(JNIEnv* env, const std::vector<jint>& v) {
    jintArray a = env->NewIntArray(jsize(v.size()));
    env->SetIntArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

jobjectArray strings(JNIEnv* env, const std::vector<std::string>& v) {
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray a = env->NewObjectArray(jsize(v.size()), cls, nullptr);
    for (size_t i = 0; i < v.size(); ++i) {
        jstring s = env->NewStringUTF(v[i].c_str());
        env->SetObjectArrayElement(a, jsize(i), s);
        env->DeleteLocalRef(s);
    }
    return a;
}

const char* const kRoots[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}  // namespace

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

FN(jdouble, act)(JNIEnv*, jobject, jint code, jint a, jint b, jdouble x) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    switch (code) {
    case togglePlay: m.togglePlay(); break;
    case toggleMetronome: m.toggleMetronome(); break;
    case undo: m.undo(); break;
    case redo: m.redo(); break;
    case setBpm: m.setBpm(x); break;
    case beginGesture: m.beginGesture(); break;
    case endGesture: m.endGesture(); break;
    case selectTrack: m.selectTrack(a); break;
    case selectChannel: m.selectChannel(jambo::Channel(a)); break;
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
    case knobDoubleTap: m.knobDoubleTap(a); break;
    case keyDown: m.keyDown(a, b, x); break;
    case keyUp: m.keyUp(a); break;
    case octaveStep: m.setOctave(m.octave() + a); break;
    case midiNote: m.midiNote(a, b); break;
    case tick: m.tick(x); break;
    default: break;
    }
    return 0;
}

/// Кадр (каждый кадр экрана): [ревизия (младшие 31 бит), ход открытого клипа, маска мигания, мигание доли,
/// акцент доли, игра].
FN(jintArray, frame)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {jint(m.revision() & 0x7fffffff), m.openPlayPos(), jint(m.flashMask()), m.beatFlash() ? 1 : 0,
                      m.beatAccent() ? 1 : 0, m.playing() ? 1 : 0});
}

/// Верхняя панель: [BPM·10, UNDO, REDO, игра, метроном].
FN(jintArray, top)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    return ints(env, {jint(std::lround(m.project().bpm * 10)), m.canUndo(), m.canRedo(), m.playing(), m.metronome()});
}

/// Подписи верхней панели: [гамма, корневая нота].
FN(jobjectArray, topTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& p = app().project();
    return strings(env, {jambo::Scales::all[size_t(p.scale)].name, kRoots[jambo::Scales::wrap(p.root, 12)]});
}

/// Дорожки: [число, выбранная, канал (0 дорожка, 1 MIX, 2 MST), по дорожке: биты mute 1, solo 2].
FN(jintArray, tracks)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<jint> v{jint(m.project().tracks.size()), m.selectedTrack(), jint(m.channel())};
    for (const auto& t : m.project().tracks) v.push_back((t.mute ? 1 : 0) | (t.solo ? 2 : 0));
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

/// Ручки: по 16 слотам [активна, положение, база, p-lock, от центра, 4-я строка] (float).
FN(jfloatArray, knobs)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(gLock);
    auto& m = app();
    std::vector<jfloat> v;
    for (int k = 0; k < 16; ++k) {
        const auto s = m.knobState(k);
        v.insert(v.end(), {s.param ? 1.f : 0.f, float(s.norm), float(s.baseNorm), s.locked ? 1.f : 0.f,
                           s.param && s.param->bipolar ? 1.f : 0.f, s.col4 ? 1.f : 0.f});
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
        v.push_back(s.param && s.param->format ? s.param->format(s.value) : "");
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
    for (int k = 0; k < m.keyCount(); ++k) v.push_back(jambo::Params::noteName(m.keyNote(k)));
    return strings(env, v);
}
