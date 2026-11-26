#include "jni_common.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

FN(void, midiRealtime)(JNIEnv*, jobject, jint status, jdouble seconds, jboolean startStop, jint offset) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().midiRealtime(status, seconds, startStop, double(offset) / 1000);
}

FN(void, midiResetClock)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().resetMidiClock();
}

// [flush, статус, метка времени в нс, …]. Метки идут в Android MidiReceiver.send.
FN(jlongArray, midiClockBatch)(JNIEnv* env, jobject, jdouble now, jboolean send, jboolean receive,
                               jboolean startStop, jint offset) {
    std::lock_guard<std::mutex> guard(jniLock());
    auto batch = jniApp().tickMidiSync(now, send, receive, startStop, double(offset) / 1000);
    if (!batch.flush && batch.events.empty()) return nullptr;
    std::vector<jlong> out{batch.flush};
    for (const auto& event : batch.events) {
        out.push_back(event.status);
        out.push_back(jlong(std::llround(event.time * 1e9)));
    }
    auto result = env->NewLongArray(jsize(out.size()));
    if (result) env->SetLongArrayRegion(result, 0, jsize(out.size()), out.data());
    return result;
}

FN(void, midiControl)(JNIEnv*, jobject, jint target, jint value, jint encoder, jdouble low, jdouble high) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().midiMappedKnob(target, value, encoder, low, high);
}

FN(void, midiButton)(JNIEnv*, jobject, jint target, jboolean down, jboolean touch, jboolean setState, jboolean global) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().midiMappedButton(target, down, touch, setState, global);
}

FN(void, midiNoteFrom)(JNIEnv*, jobject, jint note, jint velocity, jint identity) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().midiNoteFrom(note, velocity, identity);
}
