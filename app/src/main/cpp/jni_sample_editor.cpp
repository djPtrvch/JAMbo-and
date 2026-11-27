// JNI редактора сэмплов: область просмотра, метки, запись и WARP.
#include <algorithm>
#include <cmath>
#include "jni_common.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

FN(jobjectArray, sampleEditorTexts)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    auto& app = jniApp();
    return jniStrings(env, {app.editorKey(), app.editorName()});
}

FN(jfloatArray, sampleEditorWave)(JNIEnv* env, jobject, jint columns, jdouble a, jdouble b) {
    std::lock_guard<std::mutex> guard(jniLock());
    auto& app = jniApp();
    const auto key = app.editorKey();
    const auto data = app.sampleData(key);
    std::vector<float> out{0, 0, 0};
    if (data && data->frames() > 0 && columns > 0 && columns <= 2048 && std::isfinite(a) && std::isfinite(b)) {
        a = std::clamp(a, 0.0, 1.0);
        b = std::clamp(b, a, 1.0);
        const auto marks = app.project().tracks[size_t(app.selectedTrack())].sliceMarks(key);
        out = {float(data->channels()), float(data->duration()), float(marks.size())};
        for (const auto& mark : marks) out.push_back(float(mark.at));
        // Огибающая зависит от PCM, а не от маркеров или правок проекта.
        static jambo::SampleRef lastData;
        static int lastColumns = 0;
        static double lastA = 0, lastB = 1;
        static std::vector<float> peaks;
        if (data != lastData || columns != lastColumns || a != lastA || b != lastB) {
            lastA = a;
            lastB = b;
            lastData = data;
            lastColumns = columns;
            peaks.clear();
            const int frames = data->frames();
            for (int c = 0; c < data->channels(); ++c) {
                const float* pcm = c == 0 ? data->left() : data->right();
                for (int col = 0; col < columns; ++col) {
                    const int begin = std::min(frames - 1, int(frames * (a + (b - a) * col / columns)));
                    const int end = std::min(frames, std::max(begin + 1, int(frames * (a + (b - a) * (col + 1) / columns))));
                    const int stride = std::max(1, (end - begin) / 256);
                    float lo = 0, hi = 0;
                    for (int i = begin; i < end; i += stride) {
                        lo = std::min(lo, pcm[i]);
                        hi = std::max(hi, pcm[i]);
                    }
                    peaks.push_back(lo);
                    peaks.push_back(hi);
                }
            }
        }
        out.insert(out.end(), peaks.begin(), peaks.end());
    }
    const auto result = env->NewFloatArray(jsize(out.size()));
    if (result) env->SetFloatArrayRegion(result, 0, jsize(out.size()), out.data());
    return result;
}

FN(jobject, sampleEditorEdit)(JNIEnv* env, jobject, jint operation, jdouble a, jdouble b) {
    std::optional<jambo::App::EditorRemap> remap;
    {
        std::lock_guard<std::mutex> guard(jniLock());
        remap = jniApp().editorEdit(operation, a, b);
    }
    if (!remap) return nullptr;
    const auto type = env->FindClass("app/jambo/android/AppNative$EditorRemap");
    if (!type) return nullptr;
    const auto ctor = env->GetMethodID(type, "<init>", "(Ljava/lang/String;DD)V");
    if (!ctor) return nullptr;
    const auto key = env->NewStringUTF(remap->key.c_str());
    if (!key) return nullptr;
    return env->NewObject(type, ctor, key, remap->begin, remap->end);
}

FN(void, sampleEditorSlice)(JNIEnv*, jobject, jint index, jdouble at) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().editorSlice(index, at);
}

FN(void, sampleEditorPreview)(JNIEnv*, jobject, jdouble a, jdouble b) {
    std::lock_guard<std::mutex> guard(jniLock());
    if (a < 0) jniApp().stopLibraryPreview();
    else jniApp().editorPreview(a, b);
}

FN(jint, sampleEditorStatus)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    return (jniApp().editorPreviewing() ? 1 : 0) | (jniApp().sampleCapturing() ? 2 : 0);
}

FN(void, sampleEditorSave)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().editorSave();
}

FN(jboolean, sampleRecordStart)(JNIEnv*, jobject, jint source) {
    std::lock_guard<std::mutex> guard(jniLock());
    return jniApp().startSampleCapture(source == -2 ? jambo::StemRing::master : source);
}

FN(void, sampleRecordStop)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().stopSampleCapture();
}

FN(void, sampleRecordMicDone)(JNIEnv* env, jobject, jfloatArray pcm, jint frames) {
    jambo::SampleRef data;
    if (pcm && frames > 0 && frames <= env->GetArrayLength(pcm)) {
        data = std::make_shared<jambo::SampleData>(frames, false);
        env->GetFloatArrayRegion(pcm, 0, frames, data->left());
    }
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().finishSampleCapture(data);
}

FN(jfloatArray, sampleRecordPeaks)(JNIEnv* env, jobject, jint columns) {
    std::lock_guard<std::mutex> guard(jniLock());
    const auto out = jniApp().capturePeaks(columns);
    const auto result = env->NewFloatArray(jsize(out.size()));
    if (result && !out.empty()) env->SetFloatArrayRegion(result, 0, jsize(out.size()), out.data());
    return result;
}

FN(jobjectArray, sampleSliceLabels)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    return jniStrings(env, jniApp().editorSliceLabels());
}

FN(jintArray, sampleSliceRoles)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    const auto roles = jniApp().editorSliceRoles();
    const std::vector<jint> values(roles.begin(), roles.end());
    auto out = env->NewIntArray(jsize(values.size()));
    if (out && !values.empty()) env->SetIntArrayRegion(out, 0, jsize(values.size()), values.data());
    return out;
}

FN(jboolean, sampleSliceAssign)(JNIEnv*, jobject, jint index, jint key) {
    std::lock_guard<std::mutex> guard(jniLock());
    auto& app = jniApp();
    if (key >= app.keyCount() || key < -1) return false;
    return app.editorAssignSlice(index, key < 0 ? -1 : app.keyNote(key));
}

FN(void, sampleSliceRole)(JNIEnv*, jobject, jint index, jint role) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().editorSliceRole(index, role);
}

FN(void, sampleWarpAct)(JNIEnv*, jobject, jint command, jint index, jdouble a, jdouble b) {
    using Edit = jambo::App::WarpEdit;
    if (command < int(Edit::enter) || command > int(Edit::remove)) return;
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().editWarp(static_cast<Edit>(command), index, a, b);
}

FN(void, sampleWarpPreview)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    jniApp().editorWarpPreview();
}

FN(jdoubleArray, sampleWarpInfo)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> guard(jniLock());
    auto& app = jniApp();
    const auto data = app.sampleData(app.editorKey());
    const auto info = app.editorWarpInfo();
    std::vector<jdouble> values{app.editorWarpBpm(data), info.bpm.value_or(0), double(info.isOff()),
        double(app.warpFollow() && !info.isOff() && !info.marks.empty()), double(data ? data->frames() : 0)};
    for (const auto& m : info.marks) {
        values.push_back(m.at);
        values.push_back(m.beat);
    }
    auto out = env->NewDoubleArray(jsize(values.size()));
    if (out) env->SetDoubleArrayRegion(out, 0, jsize(values.size()), values.data());
    return out;
}

FN(jfloatArray, sampleWarpWave)(JNIEnv* env, jobject, jint columns, jdouble origin, jdouble endBeat) {
    std::lock_guard<std::mutex> guard(jniLock());
    auto& app = jniApp();
    const auto data = app.sampleData(app.editorKey());
    std::vector<float> out;
    if (data && columns > 0 && columns <= 2048 && std::isfinite(origin) && std::isfinite(endBeat) && endBeat > origin) {
        const int frames = data->frames();
        const jambo::WarpMap map(app.editorWarpInfo(), frames, app.editorWarpBpm(data));
        for (int c = 0; c < data->channels(); ++c) {
            const float* pcm = c == 0 ? data->left() : data->right();
            for (int col = 0; col < columns; ++col) {
                const double from = map.position(origin + (endBeat - origin) * col / columns);
                const double to = map.position(origin + (endBeat - origin) * (col + 1) / columns);
                const int begin = int(std::floor(std::clamp(from, 0.0, double(frames))));
                const int end = int(std::ceil(std::clamp(to, 0.0, double(frames))));
                const int stride = std::max(1, (end - begin) / 256);
                float low = 0, high = 0;
                for (int i = begin; i < end; i += stride) {
                    low = std::min(low, pcm[i]);
                    high = std::max(high, pcm[i]);
                }
                out.push_back(low);
                out.push_back(high);
            }
        }
    }
    auto result = env->NewFloatArray(jsize(out.size()));
    if (result && !out.empty()) env->SetFloatArrayRegion(result, 0, jsize(out.size()), out.data());
    return result;
}
