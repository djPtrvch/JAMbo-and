// JNI окон модулей мастера (AppNative): строка полей, ввод числом, пресеты, данные дисплеев. Мьютекс и App —
// jni_common.h. Шкала дисплеев (48, 32, 16 dB) — от экрана.
#include <cmath>
#include "jni_common.h"
#include "master/Clipper.h"
#include "master/Glue.h"
#include "master/MasterTables.h"
#include "model/MasterInput.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

namespace {

jfloatArray floats(JNIEnv* env, const std::vector<float>& v) {
    jfloatArray a = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

std::string oneLine(std::string s) {
    for (auto& c : s)
        if (c == '\n') c = ' ';
    return s;
}

enum WinAct {
    winOpen = 1, winClose, winUnity, presetSave, presetLoad, presetRename, presetDelete, presetMove, saveDefault,
    freeze, mixerOpen, mixerClose,
};
}  // namespace

/// Окно модуля: [модуль (−1 — закрыто), UNITY, BYPASS, полей, пресетов, SAVE DEFAULT мигает, FREEZE, окно микшера].
FN(jintArray, masterWin)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int w = m.masterWin().value_or(-1);
    const int mod = std::max(0, w);
    bool frozen = false;
    if (w == 0) frozen = m.satSpectrum().frozen;
    else if (w > 0) frozen = m.teleHistory(w).frozen;
    return jniInts(env, {w, m.masterUnity(), m.masterBypass(mod), jint(jambo::App::masterFields(mod).size()),
                         jint(m.masterPresets().list(mod).size()), m.masterDefaultFlash(mod), frozen, m.mixerOpen()});
}

/// Подписи окна: [по полю id, подпись, значение], затем имена пресетов модуля.
FN(jobjectArray, masterWinTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int mod = std::max(0, m.masterWin().value_or(0));
    std::vector<std::string> v;
    for (const auto* p : jambo::App::masterFields(mod)) {
        std::string label = p->label;
        for (auto& c : label) c = char(std::toupper((unsigned char)c));
        const std::string plain = oneLine(p->format(m.masterParam(*p)));
        v.push_back(p->id);
        v.push_back(label);
        v.push_back(p->id == "cmp.makeup" ? m.makeupLabel(plain) : plain);
    }
    for (const auto& pr : m.masterPresets().list(mod)) v.push_back(pr.name);
    return jniStrings(env, v);
}

FN(void, masterWinAct)(JNIEnv* env, jobject, jint code, jint k, jint k2, jstring jtext) {
    const std::string text = jniString(env, jtext);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int mod = std::max(0, m.masterWin().value_or(0));
    switch (code) {
    case winOpen: m.openMasterWindow(k); break;
    case winClose: m.closeMasterWindow(); break;
    case winUnity: m.setUnity(!m.masterUnity()); break;
    case presetSave: m.saveMasterPreset(mod, text, k >= 0 ? std::optional<int>(k) : std::nullopt); break;
    case presetLoad: m.loadMasterPreset(mod, k); break;
    case presetRename: m.renameMasterPreset(mod, k, text); break;
    case presetDelete: m.deleteMasterPreset(mod, k); break;
    case presetMove: m.moveMasterPreset(mod, k, k2); break;
    case saveDefault: m.saveMasterDefault(mod); break;
    case freeze:
        if (mod == 0) m.satSpectrum().frozen = !m.satSpectrum().frozen;
        else if (auto* mc = m.masterChain()) {
            const jambo::DynTele& t = mod == 1 ? mc->compTele : mod == 2 ? mc->clipTele : mc->limTele;
            auto& h = m.teleHistory(mod);
            h.setFrozen(!h.frozen, t);
        }
        break;
    case mixerOpen: m.setMixerOpen(true); break;
    case mixerClose: m.setMixerOpen(false); break;
    default: break;
    }
}

/// Пресет модуля с этим именем (без учёта регистра), кроме except; −1 — свободно. code 1 — имя нового пресета.
FN(jint, masterPresetIndex)(JNIEnv* env, jobject, jstring jname, jint except) {
    const std::string name = jniString(env, jname);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int mod = std::max(0, m.masterWin().value_or(0));
    return m.masterPresets().index(mod, name, except).value_or(-1);
}

FN(jstring, masterPresetNextName)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    return env->NewStringUTF(m.masterPresets().nextName(std::max(0, m.masterWin().value_or(0))).c_str());
}

/// Параметр мастера: code 1 — положение 0…1, 2 — значение, 3 — задать значение x, 4 — задать положение x, 5 — тап
/// (RATIO, TYPE, RELEASE, MAKEUP).
FN(jdouble, masterParam)(JNIEnv* env, jobject, jstring jid, jint code, jdouble x) {
    const std::string id = jniString(env, jid);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const jambo::ParamDef* p = jambo::Params::byId(id);
    if (!p) return 0;
    switch (code) {
    case 1: return p->toNorm(m.masterParam(*p));
    case 2: return m.masterParam(*p);
    case 3: m.setMaster(*p, x); break;
    case 4: m.setMaster(*p, p->fromNorm(std::min(1.0, std::max(0.0, x)))); break;
    case 5:
        if (id == "cmp.ratio") m.tapRatio();
        else if (id == "sat.type") m.tapSatType();
        else if (id == "cmp.release" || id == "lim.release") m.tapRelease(*p);
        else if (id == "cmp.makeup") m.tapMakeup();
        break;
    default: break;
    }
    return 0;
}

/// Ввод числом: текст поля (text пусто) или разбор text — true, если значение принято.
FN(jstring, masterInput)(JNIEnv* env, jobject, jstring jid, jstring jtext) {
    const std::string id = jniString(env, jid), text = jniString(env, jtext);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const jambo::ParamDef* p = jambo::Params::byId(id);
    if (!p) return env->NewStringUTF("");
    if (text.empty()) return env->NewStringUTF(jambo::MasterInput::text(*p, m.masterParam(*p)).c_str());
    const auto v = jambo::MasterInput::parse(*p, text);
    if (v) m.setMaster(*p, *v);
    return env->NewStringUTF(v ? "1" : "");
}

/// Дисплей окна (каждый кадр): COMP, CLIP, LIM — [cols, вход dB ×cols, выход dB ×cols, GR ×cols, последний кадр: пик
/// входа, детектор dB, GR (−999 — нет), tn, кривая TRANSFER ×tn (выход dB при входе −scale…0), GUARD dB]; SAT —
/// [cols, вход dB ×cols, выход dB ×cols, tn, кривая ×tn (выход при входе −1…+1), пик входа].
FN(jfloatArray, masterDisplay)(JNIEnv* env, jobject, jint cols, jint tn, jint scaleDb) {
    const double scale = scaleDb > 0 ? scaleDb : 48;
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int mod = m.masterWin().value_or(-1);
    jambo::MasterChain* mc = m.masterChain();
    std::vector<float> v;
    if (mod < 0 || !mc) return floats(env, v);
    const double now = m.nowSec();
    cols = std::max(1, cols);
    tn = std::max(2, tn);
    auto val = [&](const char* id, double def) { return m.masterValue(id, def); };
    if (mod == 0) {
        mc->satOn.store(1);
        auto& sp = m.satSpectrum();
        sp.update(*mc, cols, now);
        v.push_back(float(sp.dIn.size()));
        v.insert(v.end(), sp.dIn.begin(), sp.dIn.end());
        v.insert(v.end(), sp.dOut.begin(), sp.dOut.end());
        v.push_back(float(tn));
        const int type = int(std::lround(val("sat.type", 0)));
        const double drive = val("sat.drive", 0);
        for (int i = 0; i < tn; i++)
            v.push_back(float(jambo::MasterTables::shared().satCurve(type, drive, -1 + 2 * double(i) / (tn - 1))));
        std::vector<float> probe(1024);
        mc->satIn.read(probe.data(), 1024);
        float x = 0;
        for (float s : probe)
            if (std::fabs(s) > std::fabs(x)) x = s;
        v.push_back(x);
        return floats(env, v);
    }
    mc->satOn.store(0);
    const jambo::DynTele& t = mod == 1 ? mc->compTele : mod == 2 ? mc->clipTele : mc->limTele;
    auto& h = m.teleHistory(mod);
    const auto c = h.columns(t, cols, now);
    v.push_back(float(cols));
    v.insert(v.end(), c.inDb.begin(), c.inDb.end());
    v.insert(v.end(), c.outDb.begin(), c.outDb.end());
    v.insert(v.end(), c.gr.begin(), c.gr.end());
    if (const auto f = h.latest(t, now)) v.insert(v.end(), {f->inPeak, f->det, f->gr});
    else v.insert(v.end(), {-999.f, -999.f, -999.f});
    v.push_back(float(tn));
    for (int i = 0; i < tn; i++) {
        const double l = -scale + double(i) / (tn - 1) * scale;
        double out = l;
        if (mod == 1) out = l - jambo::MasterGlue::staticGr(l, val("cmp.threshold", -10), val("cmp.ratio", 2), val("cmp.knee", 6));
        else if (mod == 2) out = jambo::MasterClipper::staticOut(l, val("clip.input", 0), val("clip.ceiling", -1), val("clip.softness", 30));
        v.push_back(float(out));
    }
    v.push_back(mod == 3 ? mc->limGuardCut.load() : 0.f);
    return floats(env, v);
}

/// PRESETS и LISTEN строки EQ: [LISTEN включена, SAVE DEFAULT мигает, канал — мастер], затем имена пресетов (eqPresetNames).
FN(jintArray, eqExtra)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    return jniInts(env, {m.eqListenOn(), m.eqDefaultFlash(), m.channel() != jambo::Channel::track});
}

FN(jobjectArray, eqPresetNames)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    std::vector<std::string> v;
    for (const auto& p : jniApp().eqPresets().list) v.push_back(p.name);
    return jniStrings(env, v);
}

/// code 1 SAVE (k ≥ 0 — перезаписать k), 2 загрузить k, 3 переименовать k, 4 удалить k, 5 перенести k → k2,
/// 6 SAVE DEFAULT, 7 LISTEN.
FN(void, eqPresetAct)(JNIEnv* env, jobject, jint code, jint k, jint k2, jstring jtext) {
    const std::string text = jniString(env, jtext);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    switch (code) {
    case 1: m.saveEqPreset(text, k >= 0 ? std::optional<int>(k) : std::nullopt); break;
    case 2: m.loadEqPreset(k); break;
    case 3: m.renameEqPreset(k, text); break;
    case 4: m.deleteEqPreset(k); break;
    case 5: m.moveEqPreset(k, k2); break;
    case 6: m.saveEqDefault(); break;
    case 7: m.toggleEqListen(); break;
    default: break;
    }
}

FN(jint, eqPresetIndex)(JNIEnv* env, jobject, jstring jname, jint except) {
    const std::string name = jniString(env, jname);
    std::lock_guard<std::mutex> g(jniLock());
    return jniApp().eqPresets().index(name, except).value_or(-1);
}

FN(jstring, eqPresetNextName)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    return env->NewStringUTF(jniApp().eqPresets().nextName().c_str());
}
