// JNI мастера (AppNative): индикация COMP | CLIP | LIM, кнопки модулей, скоп. Мьютекс и App — jni_common.h.
#include <cmath>
#include "jni_common.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

static jfloatArray floats(JNIEnv* env, const std::vector<float>& v) {
    jfloatArray a = env->NewFloatArray(jsize(v.size()));
    env->SetFloatArrayRegion(a, 0, jsize(v.size()), v.data());
    return a;
}

/// Мастер: [bypass SAT, COMP, CLIP, LIM, точки скопа SAT, COMP, CLIP, LIM].
FN(jintArray, masterInfo)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    std::vector<jint> v;
    for (int k = 0; k < 4; k++) v.push_back(m.masterBypass(k) ? 1 : 0);
    for (int k = 0; k < 4; k++) v.push_back(m.scopeOn()[size_t(k)] ? 1 : 0);
    return jniInts(env, v);
}

/// code 1 — кнопка модуля k (с SHIFT — BYPASS), 2 — точка скопа k.
FN(void, masterAct)(JNIEnv*, jobject, jint code, jint k) {
    std::lock_guard<std::mutex> g(jniLock());
    auto& m = jniApp();
    if (code == 1) m.masterButton(k);
    else if (code == 2) m.toggleScope(k);
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
/// слышна, линейные L и R (PAN)].
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
    }
    return floats(env, v);
}
