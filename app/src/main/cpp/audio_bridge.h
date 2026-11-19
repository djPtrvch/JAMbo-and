// Общий звук для JNI: один AudioEngine (и Link) на процесс — jni_bridge.cpp.
#pragma once
class AudioEngine;
class LinkSession;
AudioEngine* audioEngine();
