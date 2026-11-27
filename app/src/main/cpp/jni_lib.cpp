// JNI библиотеки (AppNative): браузер, IMPORT (декодирование — в потоке экрана, вне мьютекса), форма волны окна
// сэмпла. Мьютекс и App — общие с jni_app.cpp (jni_common.h).
#include <algorithm>
#include <cmath>
#include "jni_common.h"
#include "model/SynthEngines.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

namespace {
enum LibAct { libOpen = 1, libClose, libEnterF, libUpF, libTapF, libHoldF, libPickAllF, libCancelPickF, libLoadPickedF, libPreviewF, libNewFolderF, libRenameF, libDeleteF, libSaveF, libOpenSaveF };
}

/// Браузер: [открыт, в корне, выбор нескольких, отмечено, шагов загрузки, строк, SAVE].
FN(jintArray, lib)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    if (!m.browserOpen()) return jniInts(env, {0, 1, 0, 0, 0, 0, 0});
    const auto& p = m.libPicked();
    return jniInts(env, {1, m.libAtRoot(), p ? 1 : 0, p ? jint(p->size()) : 0, jint(m.browserSteps().size()),
                         jint(m.libItems().size()), m.libSaveMode() ? 1 : 0, m.libRefMode() ? 1 : 0});
}

/// Строки браузера: [путь, подпись внизу, затем по строке: id, вид (f, s, p), имя, справа (число в папке, движок),
/// звук дорожки («1»), отмечен («1»)].
FN(jobjectArray, libRows)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    std::vector<std::string> v{m.libPath(), m.libNote()};
    if (!m.browserOpen()) return jniStrings(env, v);
    const auto& picked = m.libPicked();
    for (const auto& n : m.libItems()) {
        std::string kind = n.kind == jambo::LibKind::folder ? "f" : n.kind == jambo::LibKind::sample ? "s" : "p";
        std::string meta;
        if (n.kind == jambo::LibKind::folder) meta = std::to_string(m.library().countInside(n.id));
        else if (n.kind == jambo::LibKind::preset && n.preset) {
            const int e = jambo::SynthKey::engine(n.preset->sound);
            if (const auto* en = jambo::SynthEngines::engine(e)) meta = en->name;
        }
        v.push_back(n.id);
        v.push_back(kind);
        v.push_back(n.name);
        v.push_back(meta);
        v.push_back(m.libIsCurrent(n) ? "1" : "");
        v.push_back(picked && picked->count(n.id) ? "1" : "");
    }
    return jniStrings(env, v);
}

FN(void, libAct)(JNIEnv* env, jobject, jint code, jstring jid, jstring jtext) {
    const std::string id = jniString(env, jid), text = jniString(env, jtext);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    switch (code) {
    case libOpen: {
        const auto& menu = m.soundMenu();
        const std::vector<int> steps = menu ? *menu : std::vector<int>{};
        m.openBrowser(steps);
        break;
    }
    case libClose: m.closeBrowser(); break;
    case libEnterF: m.libEnter(id); break;
    case libUpF: m.libUp(); break;
    case libTapF: m.libTap(id); break;
    case libHoldF: m.libHold(id); break;
    case libPickAllF: m.libPickAll(); break;
    case libCancelPickF: m.libCancelPick(); break;
    case libLoadPickedF: m.libLoadPicked(); break;
    case libPreviewF: m.libPreview(id); break;
    case libNewFolderF: m.libNewFolder(text); break;
    case libRenameF: m.libRename(id, text); break;
    case libDeleteF: m.libDelete(id); break;
    case libSaveF: m.libSave(text, id); break;
    case libOpenSaveF: m.openBrowserSave(); break;
    default: break;
    }
}

/// Подписи SAVE: code 1 — имя по умолчанию, 2 — id узла с именем text (пусто — свободно), 3 — свободное имя.
FN(jstring, libText)(JNIEnv* env, jobject, jint code, jstring jtext) {
    const std::string text = jniString(env, jtext);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    std::string out;
    if (code == 1) out = m.libDefaultName();
    else if (code == 2) {
        if (const auto* n = m.libSameName(text)) out = n->id;
    } else if (code == 3) out = m.libFreeName(text);
    return env->NewStringUTF(out.c_str());
}

/// Папка IMPORT (запоминается экраном до конца импорта).
FN(jstring, libImportFolder)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    return env->NewStringUTF(jniApp().libImportFolder().c_str());
}

static std::string samplesDir() {
    std::lock_guard<std::mutex> g(jniLock());
    const auto* st = jniApp().sampleStore();
    return st ? st->dir() : std::string();
}

/// IMPORT файла (поток экрана): 1 — сэмпл, 3 — пресет, 2 — формат не WAV/AIFF/CAF (экран декодирует сам,
/// libImportPcm), 0 — не удалось.
FN(jint, libImportBytes)(JNIEnv* env, jobject, jstring jname, jbyteArray bytes, jstring jfolder) {
    const std::string name = jniString(env, jname), folder = jniString(env, jfolder);
    const jsize n = env->GetArrayLength(bytes);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(data.data()));
    std::string lower = name;
    for (auto& c : lower) c = char(std::tolower((unsigned char)c));
    if (lower.size() > 12 && lower.compare(lower.size() - 12, 12, ".jambopreset") == 0) {
        std::lock_guard<std::mutex> g(jniLock());
        return jniApp().libImportPreset(folder, name, std::string(data.begin(), data.end())) ? 3 : 0;
    }
    const std::string dir = samplesDir();
    if (dir.empty()) return 0;
    bool need = false;
    auto l = jambo::SampleStore::decode(data.data(), data.size(), name, dir, &need);
    if (!l) return need ? 2 : 0;
    std::lock_guard<std::mutex> g(jniLock());
    jniApp().libImported(folder, *l);
    return 1;
}

/// Декодированный экраном PCM (чередование каналов); key — SHA-256 исходных байтов (24 hex-знака).
FN(jint, libImportPcm)(JNIEnv* env, jobject, jstring jname, jstring jkey, jfloatArray pcm, jint channels, jint rate,
                       jstring jfolder) {
    const std::string name = jniString(env, jname), key = jniString(env, jkey), folder = jniString(env, jfolder);
    const jsize n = env->GetArrayLength(pcm);
    std::vector<float> data(static_cast<size_t>(n));
    env->GetFloatArrayRegion(pcm, 0, n, data.data());
    const std::string dir = samplesDir();
    if (dir.empty() || channels <= 0) return 0;
    auto l = jambo::SampleStore::fromPcm(key, name, data.data(), n / channels, channels, rate, dir);
    if (!l) return 0;
    std::lock_guard<std::mutex> g(jniLock());
    jniApp().libImported(folder, *l);
    return 1;
}

FN(void, libNote)(JNIEnv* env, jobject, jstring text) {
    const std::string s = jniString(env, text);
    std::lock_guard<std::mutex> g(jniLock());
    jniApp().setLibNote(s);
}

/// Форма волны звука окна (сэмпл): [каналов (0 — не сэмпл), Start, End (доли), затем по каналу cols пиков по модулю].
FN(jfloatArray, wave)(JNIEnv* env, jobject, jint cols) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const std::string key = m.shownSound();
    const auto d = key.empty() ? nullptr : m.sampleData(key);
    // Последняя огибающая — по сэмплу и числу столбцов (ревизия меняется на каждом повороте ручки).
    static jambo::SampleRef lastData;
    static int lastCols = 0;
    static std::vector<float> last;
    if (!(d == lastData && cols == lastCols)) {
        last.clear();
        if (d && cols > 0 && d->frames() > 0) {
            const int f = d->frames();
            for (int c = 0; c < d->channels(); c++) {
                const float* x = c == 0 ? d->left() : d->right();
                for (int k = 0; k < cols; k++) {
                    const int a = int(int64_t(f) * k / cols), b = std::max(a + 1, int(int64_t(f) * (k + 1) / cols));
                    float pk = 0;
                    for (int i = a; i < b && i < f; i++) pk = std::max(pk, std::fabs(x[i]));
                    last.push_back(pk);
                }
            }
        }
        lastData = d;
        lastCols = cols;
    }
    std::vector<float> v{0, 0, 1};
    if (!last.empty()) {
        const auto& p = m.project().tracks[size_t(m.selectedTrack())].params;
        const auto ps = p.find("start"), pe = p.find("end");
        const double s = (ps == p.end() ? 0 : ps->second) / 100;
        const double e = std::max(s, (pe == p.end() ? 100 : pe->second) / 100);
        v = {float(d->channels()), float(s), float(e)};
        v.insert(v.end(), last.begin(), last.end());
    }
    jfloatArray arr = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(arr, 0, jsize(v.size()), v.data());
    return arr;
}

/// Полосы воспроизведения окна сэмпла (каждый кадр, пока звучат голоса): пары доля, непрозрачность.
FN(jfloatArray, heads)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    std::vector<float> v;
    if (m.channel() == jambo::Channel::track)
        for (const auto& [f, a] : m.playheads(m.selectedTrack(), m.shownSound())) {
            v.push_back(float(f));
            v.push_back(float(a));
        }
    jfloatArray arr = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(arr, 0, jsize(v.size()), v.data());
    return arr;
}
