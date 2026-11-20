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
    keyDown = 80, keyUp, octaveStep, midiNote,
    setSource = 85, knobFinger, assignSource, assignStart, setAssign,
    openSoundMenu = 90, closeSoundMenu, pickSynth, cleanSampler, stepSoundToTrack, folderArrow,
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
    case octaveStep: m.setOctave(m.octave() + a); break;
    case midiNote: m.midiNote(a, b); break;
    case openSoundMenu: m.openSoundMenu(); break;
    case closeSoundMenu: m.closeSoundMenu(); break;
    case pickSynth: m.pickSynth(a); break;
    case cleanSampler: m.cleanSampler(); break;
    case stepSoundToTrack: m.stepSoundToTrack(); break;
    case folderArrow: m.folderArrow(a); break;
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
    for (int k = 0; k < m.keyCount(); ++k) v.push_back(jambo::Params::noteName(m.keyNote(k)));
    return strings(env, v);
}

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
