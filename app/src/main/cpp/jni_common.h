// Общее для JNI экрана (jni_app.cpp, jni_lib.cpp): один мьютекс на вызовы из UI- и MIDI-потоков, App, массивы.
#pragma once
#include <jni.h>
#include <mutex>
#include <string>
#include <vector>
#include "app/App.h"

std::mutex& jniLock();
jambo::App& jniApp();
jintArray jniInts(JNIEnv* env, const std::vector<jint>& v);
jobjectArray jniStrings(JNIEnv* env, const std::vector<std::string>& v);
std::string jniString(JNIEnv* env, jstring s);
