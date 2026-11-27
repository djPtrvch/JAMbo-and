// JNI аранжировщика: состояние таймлайна, правки блоков и кривые AUTO.
#include "jni_common.h"

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

namespace {
enum class Command {
    toggle, seek, select, add, insert, move, resize, duplicate, remove, split, grid, snap,
    loop, loopRange, steps, autoPanel, row, color, section, sectionStart, sectionEnd, deleteSection
};
}

FN(void, arrangeAct)(JNIEnv*, jobject, jint command, jint a, jint b, jint c, jint d) {
    std::lock_guard<std::mutex> lock(jniLock());
    auto& app = jniApp();
    switch (static_cast<Command>(command)) {
    case Command::toggle: app.toggleArrange(); break;
    case Command::seek: app.seekArrange(a); break;
    case Command::select: app.selectArrangeBlock(a, b); break;
    case Command::add: app.addArrangeBlock(a, b, c, false); break;
    case Command::insert: app.addArrangeBlock(a, b, c, true); break;
    case Command::move: app.moveArrangeBlock(a, b, c); break;
    case Command::resize: app.resizeArrangeBlock(a, b, c, d); break;
    case Command::duplicate: app.duplicateArrangeBlock(a, b); break;
    case Command::remove: app.deleteArrangeBlock(a, b); break;
    case Command::split: app.splitArrangeBlock(); break;
    case Command::grid: app.setArrangeGrid(a); break;
    case Command::snap: app.toggleArrangeSnap(); break;
    case Command::loop: app.toggleArrangeLoop(); break;
    case Command::loopRange: app.setArrangeLoop(a, b); break;
    case Command::steps: app.toggleArrangeSteps(); break;
    case Command::autoPanel: app.toggleArrangeAuto(); break;
    case Command::row: app.sendRowToArrange(a, b < 0 ? std::nullopt : std::optional<int>(b)); break;
    case Command::color: app.setArrangeTrackColor(a, b); break;
    case Command::section: app.addArrangeSection(a); break;
    case Command::sectionStart: app.resizeArrangeSection(a, b, true); break;
    case Command::sectionEnd: app.resizeArrangeSection(a, b, false); break;
    case Command::deleteSection: app.deleteArrangeSection(a); break;
    }
}

FN(jintArray, arrangeData)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& app = jniApp();
    if (!app.arrangeView()) return jniInts(env, {0});
    const auto& p = app.project();
    const auto& a = p.arrange;
    const auto selected = app.arrangeSelection();
    std::vector<jint> values{1, a.grid, int(a.snap), int(a.hideSteps), int(app.arrangeAuto()),
        int(a.loop), a.loopFrom, a.loopTo, p.arrangeEnd(), selected ? selected->first : -1,
        selected ? selected->second : -1, jint(p.tracks.size()), jint(a.markers.size())};
    for (const auto& marker : a.markers)
        values.insert(values.end(), {marker.at, marker.end.value_or(-1), marker.color});
    for (const auto& track : p.tracks) {
        values.insert(values.end(), {track.color, int(track.mute), int(track.solo), jint(track.blocks.size())});
        for (const auto& block : track.blocks)
            values.insert(values.end(), {block.start, block.length, block.clip, block.offset});
    }
    return jniInts(env, values);
}

FN(jint, arrangePosition)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    return jniApp().arrangePosition();
}

FN(jobjectArray, arrangeSections)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    std::vector<std::string> names;
    for (const auto& marker : jniApp().project().arrange.markers) names.push_back(marker.name);
    return jniStrings(env, names);
}

FN(void, arrangeRenameSection)(JNIEnv* env, jobject, jint index, jstring title) {
    const auto name = jniString(env, title);
    std::lock_guard<std::mutex> lock(jniLock());
    jniApp().renameArrangeSection(index, name);
}

FN(jintArray, arrangeClips)(JNIEnv* env, jobject, jint track) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& p = jniApp().project();
    std::vector<jint> ids;
    if (track >= 0 && track < int(p.tracks.size())) {
        const auto& t = p.tracks[size_t(track)];
        for (int i = 0; i < p.visibleRows(); ++i) if (t.clipFilled(i)) ids.push_back(i);
    }
    return jniInts(env, ids);
}

FN(jobjectArray, arrangeAutoParams)(JNIEnv* env, jobject, jint track, jboolean existing) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& p = jniApp().project();
    if (track < 0 || track >= int(p.tracks.size())) return jniStrings(env, {});
    const auto& t = p.tracks[size_t(track)];
    std::vector<std::string> params;
    const auto grid = jambo::Params::grid(t.sample);
    if (existing) {
        for (const auto& lane : t.autos) {
            const auto item = std::find_if(grid.begin(), grid.end(), [&](const auto& v) { return v.id == lane.param; });
            params.insert(params.end(), {lane.param, item == grid.end() ? lane.param : item->label});
        }
    } else {
        for (const auto& def : grid)
            if (jambo::ArrAutoTarget::index(def.id) >= 0) params.insert(params.end(), {def.id, def.label});
    }
    return jniStrings(env, params);
}

FN(jdoubleArray, arrangeAutoPoints)(JNIEnv* env, jobject, jint track, jstring lane) {
    const auto id = jniString(env, lane);
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& p = jniApp().project();
    std::vector<jdouble> values;
    if (track >= 0 && track < int(p.tracks.size())) {
        for (const auto& pt : p.tracks[size_t(track)].autoPoints(id))
            values.insert(values.end(), {pt.x, pt.y});
    }
    const auto array = env->NewDoubleArray(jsize(values.size()));
    if (array && !values.empty()) env->SetDoubleArrayRegion(array, 0, jsize(values.size()), values.data());
    return array;
}

FN(void, arrangeSetAuto)(JNIEnv* env, jobject, jint track, jstring lane, jdoubleArray data) {
    if (!data) return;
    const jsize count = env->GetArrayLength(data);
    if (count % 2 != 0 || count / 2 > jambo::ArrAutoLane::maxPoints) return;
    std::vector<jdouble> values(size_t(count), 0);
    if (count != 0) env->GetDoubleArrayRegion(data, 0, count, values.data());
    if (env->ExceptionCheck()) return;
    const auto id = jniString(env, lane);
    std::vector<jambo::ArrAutoPoint> points;
    for (int i = 0; i < count; i += 2) points.push_back({values[size_t(i)], values[size_t(i + 1)]});
    std::lock_guard<std::mutex> lock(jniLock());
    jniApp().setArrangeAuto(track, id, points);
}

FN(void, arrangeDeleteAuto)(JNIEnv* env, jobject, jint track, jstring lane) {
    const auto id = lane ? jniString(env, lane) : std::string{};
    std::lock_guard<std::mutex> lock(jniLock());
    if (lane) jniApp().deleteArrangeAuto(track, id);
    else jniApp().clearArrangeAuto(track);
}
