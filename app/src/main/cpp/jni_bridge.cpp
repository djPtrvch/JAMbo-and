// JNI: app.jambo.android.Native
#include <jni.h>
#include "audio_engine.h"
#include "link_session.h"

namespace {
LinkSession* gLink = nullptr;
AudioEngine* gAudio = nullptr;
void ensure() {
    if (!gLink) gLink = new LinkSession();
    if (!gAudio) gAudio = new AudioEngine(gLink);
}
}

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_Native_##name

FN(jboolean, start)(JNIEnv*, jobject) { ensure(); return gAudio->start(); }
FN(void, stop)(JNIEnv*, jobject) { if (gAudio) gAudio->stop(); }
FN(void, setTone)(JNIEnv*, jobject, jboolean on) { ensure(); gAudio->setTone(on); }
FN(void, tap)(JNIEnv*, jobject, jlong touchNs) { ensure(); gAudio->tap(touchNs); }
FN(void, noteOn)(JNIEnv*, jobject, jint n, jint v) { ensure(); gAudio->noteOn(n, v); }
FN(void, noteOff)(JNIEnv*, jobject, jint n) { ensure(); gAudio->noteOff(n); }

FN(jdoubleArray, audioInfo)(JNIEnv* env, jobject) {
    ensure();
    double v[AudioEngine::kInfoSize];
    gAudio->info(v);
    jdoubleArray a = env->NewDoubleArray(AudioEngine::kInfoSize);
    env->SetDoubleArrayRegion(a, 0, AudioEngine::kInfoSize, v);
    return a;
}

FN(void, linkEnable)(JNIEnv*, jobject, jboolean on) { ensure(); gLink->enable(on); }
FN(void, linkSetTempo)(JNIEnv*, jobject, jdouble bpm) { ensure(); gLink->setTempo(bpm); }
FN(jdoubleArray, linkInfo)(JNIEnv* env, jobject, jlong outLatencyUs) {
    ensure();
    double v[5];
    gLink->info(v, outLatencyUs);
    jdoubleArray a = env->NewDoubleArray(5);
    env->SetDoubleArrayRegion(a, 0, 5, v);
    return a;
}
