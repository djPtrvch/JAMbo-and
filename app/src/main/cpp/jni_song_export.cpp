// Экспорт песни запускается в фоновом потоке. Мьютекс App нужен только для получения снимка и завершения.
#include "jni_common.h"
#include "song_encode.h"
#include <atomic>

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

namespace {
std::atomic<bool> cancelSong{false};
std::atomic<int> songProgress{0};
// Под jniLock: подготовка и рендер занимают один слот на приложение.
std::optional<jambo::SongRenderSource> pendingSong;
bool runningSong = false;
}

FN(jdouble, songSeconds)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& p = jniApp().project();
    return p.arrangeEnd() * 15.0 / p.bpm;
}

FN(jboolean, prepareSong)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    if (pendingSong || runningSong) return false;
    try {
        pendingSong = jniApp().beginSongExport();
    } catch (...) {
        jniApp().endSongExport();
        return false;
    }
    if (!pendingSong) return false;
    cancelSong.store(false, std::memory_order_relaxed);
    songProgress.store(0, std::memory_order_relaxed);
    return true;
}

FN(jint, renderSongFile)(JNIEnv* env, jobject, jstring path, jint codec, jint depth, jint rate, jint tail) {
    std::optional<jambo::SongRenderSource> source;
    {
        std::lock_guard<std::mutex> lock(jniLock());
        if (!pendingSong) return -1;
        runningSong = true;
        source = std::move(pendingSong);
        pendingSong.reset();
    }
    const auto cancelled = [] { return cancelSong.load(std::memory_order_relaxed); };
    bool ok = false;
    try {
        const auto dest = jniString(env, path);
        if (!dest.empty() && !cancelled() && codec >= 0 && codec <= 3) {
            ok = jambo::encodeSong(dest, {static_cast<jambo::SongCodec>(codec), depth, rate}, [&](const auto& sink) {
                return jambo::renderSong(std::move(*source), tail, sink, cancelled, [](double progress) {
                    songProgress.store(int(progress * 1000), std::memory_order_relaxed);
                });
            });
        }
    } catch (...) {
        ok = false;
    }
    int result;
    {
        std::lock_guard<std::mutex> lock(jniLock());
        result = cancelled() ? 0 : ok ? 1 : -1;
        if (result == 1) songProgress.store(1000, std::memory_order_relaxed);
        jniApp().endSongExport();
        runningSong = false;
    }
    return result;
}

FN(jint, songProgress)(JNIEnv*, jobject) { return songProgress.load(std::memory_order_relaxed); }
FN(void, cancelSong)(JNIEnv*, jobject) { cancelSong.store(true, std::memory_order_relaxed); }
