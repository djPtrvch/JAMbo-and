#include "jni_common.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

FN(jint, exportProjectPack)(JNIEnv* env, jobject, jstring path) {
    try {
        struct Snapshot {
            jambo::Project project;
            std::string name;
            std::optional<jambo::ProjectPack::Storage> storage;
        };
        const auto snapshot = [] {
            std::lock_guard<std::mutex> guard(jniLock());
            const auto& app = jniApp();
            return Snapshot{app.project(), app.session().name, app.projectPackStorage()};
        }();
        if (!snapshot.storage) return -1;
        const auto missing = jambo::ProjectPack::write(jniString(env, path), snapshot.name,
                                                         snapshot.project, *snapshot.storage);
        return missing ? jint(*missing) : -1;
    } catch (const std::exception&) {
        return -1;
    }
}

FN(jobjectArray, importProjectPack)(JNIEnv* env, jobject, jstring path) {
    try {
        const auto storage = [] {
            std::lock_guard<std::mutex> guard(jniLock());
            return jniApp().projectPackStorage();
        }();
        if (!storage) return jniStrings(env, {"", "Project storage is unavailable."});
        const auto imported = jambo::ProjectPack::read(jniString(env, path), *storage);
        if (!imported) return jniStrings(env, {"", "This file is not a supported JAMbo project or is damaged."});
        std::lock_guard<std::mutex> guard(jniLock());
        const auto saved = jniApp().saveImportedProject(*imported);
        if (!saved) return jniStrings(env, {"", "Could not save the imported project."});
        return jniStrings(env, {saved->id, saved->name, std::to_string(imported->missing)});
    } catch (const std::exception&) {
        return jniStrings(env, {"", "Could not read or save the project."});
    }
}
