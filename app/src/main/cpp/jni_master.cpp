// JNI мастера (AppNative): индикация COMP | CLIP | LIM, кнопки модулей, скоп. Мьютекс и App — jni_common.h.
#include <cmath>
#include "jni_common.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

static jfloatArray floats(JNIEnv* env, const std::vector<float>& v) {
    jfloatArray a = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

/// Мастер: [bypass SAT, COMP, CLIP, LIM, точки скопа SAT, COMP, CLIP, LIM, вид полос MIX].
FN(jintArray, masterInfo)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    std::vector<jint> v;
    for (int k = 0; k < 4; k++) v.push_back(m.masterBypass(k) ? 1 : 0);
    for (int k = 0; k < 4; k++) v.push_back(m.scopeOn()[size_t(k)] ? 1 : 0);
    v.push_back(m.mixMode());
    return jniInts(env, v);
}

/// code 1 — кнопка модуля k (с SHIFT — BYPASS), 2 — точка скопа k, 3 — вид полос MIX, 4 — сброс стерео по полосам, 5 — сброс истории громкости, 6 — BYPASS модуля.
FN(void, masterAct)(JNIEnv*, jobject, jint code, jint k) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    if (code == 1) m.masterButton(k);
    else if (code == 2) m.toggleScope(k);
    else if (code == 3) m.setMixMode(k);
    else if (code == 6) m.toggleMasterBypass(k);
    else if (code == 4) m.resetBandStereo();
    else if (code == 5) m.resetLoudHistory();
}

/// Индикация (каждый кадр при MST): по блоку COMP, CLIP, LIM — [IN dB, OUT dB, GR dB, IN красный, OUT красный,
/// bypass, риска dB].
FN(jfloatArray, masterBars)(JNIEnv* env, jobject, jdouble now) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const auto& mm = m.masterMeters();
    const jambo::DynBarsState* st[3] = {&mm.comp, &mm.clip, &mm.lim};
    const double marks[3] = {m.masterValue("cmp.threshold", -10), m.masterValue("clip.ceiling", -1),
                             m.masterValue("lim.ceiling", -1)};
    std::vector<float> v;
    for (int k = 0; k < 3; k++) {
        const auto& s = *st[k];
        v.push_back(s.inDb);
        v.push_back(s.outDb);
        v.push_back(s.gr);
        v.push_back(s.inDb > 0 || now < s.inOverUntil ? 1.f : 0.f);
        v.push_back(s.outDb > 0 || now < s.outOverUntil ? 1.f : 0.f);
        v.push_back(m.masterBypass(k + 1) ? 1.f : 0.f);
        v.push_back(float(marks[k]));
    }
    return floats(env, v);
}

/// Скоп (каждый кадр при MIX и MST; width — точек осциллоскопа): [корреляция (−9 — тишина), точек гониометра n,
/// n пар side, mid (±1,2), затем по точке съёма 0…3: столбцов w (0 — выключена), w мин., w макс. по порядку].
FN(jfloatArray, scopeData)(JNIEnv* env, jobject, jint width) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    m.setScopeWidth(width);
    const auto& s = m.scope();
    std::vector<float> v{s.corr ? *s.corr : -9.f};
    if (s.gonioOk) {
        const int n = jambo::ScopeState::gonioN / 2;
        v.push_back(float(n));
        for (int i = 0; i < jambo::ScopeState::gonioN; i += 2) {
            const float l = s.gl[size_t(i)], r = s.gr[size_t(i)];
            v.push_back(std::max(-1.2f, std::min(1.2f, (l - r) * 0.70710678f)));
            v.push_back(std::max(-1.2f, std::min(1.2f, (l + r) * 0.70710678f)));
        }
    } else {
        v.push_back(0);
    }
    for (int k = 0; k < 4; k++) {
        const auto& lo = s.mins[size_t(k)];
        const auto& hi = s.maxs[size_t(k)];
        const int n = int(lo.size());
        if (!m.scopeOn()[size_t(k)] || n == 0) {
            v.push_back(0);
            continue;
        }
        v.push_back(float(n));
        for (int x = 0; x < n; x++) v.push_back(lo[size_t((s.pos[size_t(k)] + x) % n)]);
        for (int x = 0; x < n; x++) v.push_back(hi[size_t((s.pos[size_t(k)] + x) % n)]);
    }
    return floats(env, v);
}

/// Обзор MIX (каждый кадр при MIX): [дорожек n, по дорожке L, R, холд L, холд R (доли шкалы −60…0 dBFS), перегруз,
/// слышна, линейные L и R (PAN), LOUD (доля), FREQ: низ, верх, пик (−1 — тишина), мешающая, 30 сил MASK, 30 «заглушают»].
FN(jfloatArray, mixBars)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int n = std::min(int(m.project().tracks.size()), jambo::MeterState::channels);
    auto frac = [](float p) { return p <= 0 ? 0.f : std::min(1.f, std::max(0.f, (20 * std::log10(p) + 60) / 60)); };
    std::vector<float> v{float(n)};
    for (int t = 0; t < n; t++) {
        const auto lr = m.meters().bars(t, true);
        const float l = lr.size() > 0 ? lr[0].level : 0, r = lr.size() > 1 ? lr[1].level : l;
        const float hl = lr.size() > 0 ? lr[0].hold : 0, hr = lr.size() > 1 ? lr[1].hold : hl;
        v.insert(v.end(), {frac(l), frac(r), frac(hl), frac(hr), std::max(l, r) >= jambo::MeterState::over ? 1.f : 0.f,
                           m.project().audible(t) ? 1.f : 0.f});
        v.push_back(l);
        v.push_back(r);
        const auto& an = m.mixAnalysis();
        v.push_back(frac(an.loud(t)));
        const auto sp = an.span(t);
        v.insert(v.end(), {sp ? sp->lo : -1.f, sp ? sp->hi : -1.f, sp ? sp->peak : -1.f});
        const auto mk = an.mask(t);
        v.push_back(float(mk.who));
        v.insert(v.end(), mk.strength.begin(), mk.strength.end());
        for (bool b : mk.buried) v.push_back(b ? 1.f : 0.f);
    }
    return floats(env, v);
}

/// Справа при MIX: [по полосе LOW, MID, HIGH: есть сигнал, корреляция, ширина %; short-term, интегральная (LUFS, −999 —
/// нет), PLR (−999 — нет), LRA (−1 — нет), 300 значений short-term от старых к новым (−999 — тишина)].
FN(jfloatArray, mixStereo)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const auto& b = m.bandStereo();
    std::vector<float> v;
    for (size_t k = 0; k < 3; k++) v.insert(v.end(), {b.live[k] ? 1.f : 0.f, b.corr[k], b.width[k]});
    const auto& h = m.loudHistory();
    float st = -INFINITY, in = -INFINITY;
    if (const auto* lv = m.levelsNow()) {
        st = lv->mixLoudness.shortTerm.load();
        in = lv->mixLoudness.integrated.load();
    }
    auto fin = [](float x) { return std::isfinite(x) ? x : -999.f; };
    const float plr = h.peak > 0 && std::isfinite(in) ? 20 * std::log10(h.peak) - in : -999.f;
    v.insert(v.end(), {fin(st), fin(in), plr, float(h.lra)});
    for (int k = 0; k < jambo::LoudHistory::points; k++) {
        const float x = h.values[size_t((h.head + k) % jambo::LoudHistory::points)];
        v.push_back(std::isfinite(x) && x > -70 ? x : -999.f);
    }
    return floats(env, v);
}

/// Окно микшера (каждый кадр): [n, по дорожке (10): стерео, заглушена, L, холд L, R, холд R, Sidechain dB, клиппинг,
/// наибольший пик, громкость (ручка MIX, dB)].
FN(jfloatArray, mixerData)(JNIEnv* env, jobject, jdouble now) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const int n = std::min(int(m.project().tracks.size()), jambo::MeterState::channels);
    std::vector<float> v{float(n)};
    for (int t = 0; t < n; t++) {
        const bool stereo = m.trackStereo(t);
        const auto b = m.meters().bars(t, stereo);
        const float l = b.size() > 0 ? b[0].level : 0, hl = b.size() > 0 ? b[0].hold : 0;
        const float r = b.size() > 1 ? b[1].level : 0, hr = b.size() > 1 ? b[1].hold : 0;
        v.insert(v.end(), {stereo && b.size() > 1 ? 1.f : 0.f, m.project().audible(t) ? 0.f : 1.f, l, hl, r, hr,
                           m.meters().duckDb(t), m.meters().clip(t, now) ? 1.f : 0.f, m.meters().maxPeak[size_t(t)],
                           float(m.knobState(t).value)});
    }
    return floats(env, v);
}

FN(void, mixerResetPeak)(JNIEnv*, jobject, jint ch) {
    std::lock_guard<std::mutex> g(jniLock());
    jniApp().resetTrackPeak(ch);
}

/// Микшер: [слот меню дорожки (−1 — нет), меню ADD, есть скопированная, можно удалить слот меню, дорожек n,
/// по дорожке: mute, solo, ● горит, точка записи (0 — нет, 1 — пишет, 2 — до конца меньше 5 с); MASTER: ● горит, точка].
FN(jintArray, mixInfo)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    const auto& tm = m.trackMenu();
    const int n = int(m.project().tracks.size());
    std::vector<jint> v{tm ? tm->slot : -1, tm && tm->add ? 1 : 0, m.hasTrackClip() ? 1 : 0,
                        tm && m.project().canDeleteTrack(tm->slot) ? 1 : 0, n};
    auto dot = [&](int ch) {
        const auto d = m.stemDot(ch);
        return d.first ? (d.second ? 2 : 1) : 0;
    };
    for (int t = 0; t < n; t++) {
        const auto& tr = m.project().tracks[size_t(t)];
        v.insert(v.end(), {tr.mute ? 1 : 0, tr.solo ? 1 : 0, m.stemLit(t) ? 1 : 0, dot(t)});
    }
    v.insert(v.end(), {m.stemLit(jambo::StemRing::master) ? 1 : 0, dot(jambo::StemRing::master)});
    return jniInts(env, v);
}

/// code 1 mute, 2 solo, 3 ● (a — канал, −1 — MASTER), 4 меню дорожки, 5 закрыть меню, 6 ADD TRACK, 7 COPY, 8 PASTE,
/// 9 DELETE TRACK, 10 RENAME (text), 11 перестановка a → b.
FN(void, mixAct)(JNIEnv* env, jobject, jint code, jint a, jint b, jstring jtext) {
    const std::string text = jniString(env, jtext);
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    switch (code) {
    case 1: m.toggleMute(a); break;
    case 2: m.toggleSolo(a); break;
    case 3: m.stemTap(a < 0 ? jambo::StemRing::master : a); break;
    case 4: m.openTrackMenu(a); break;
    case 5: m.closeTrackMenu(); break;
    case 6: m.addTrack(); break;
    case 7: m.copyTrack(a); break;
    case 8: m.pasteTrack(a); break;
    case 9: m.deleteTrack(a); break;
    case 10: m.renameTrack(a, text); break;
    case 11: m.moveTrack(a, b); break;
    default: break;
    }
}
