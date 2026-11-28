// JNI аранжировщика: состояние таймлайна, правки блоков и кривые AUTO.
#include "jni_common.h"
#include <cmath>
#include <limits>

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_app_jambo_android_AppNative_##name

namespace {
enum class Command {
    toggle, seek, select, add, insert, move, resize, duplicate, remove, split, grid, snap,
    loop, loopRange, steps, autoPanel, row, color, section, sectionStart, sectionEnd, deleteSection, record,
    copyBlock, pasteBlock, trackClipboard, refToggle, refLoad, refDelete, refB, clipToArrange, carry
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
    case Command::record: app.toggleArrangeRecord(); break;
    case Command::copyBlock: app.copyArrangeBlock(a, b); break;
    case Command::pasteBlock: app.pasteArrangeBlock(a, b, c); break;
    case Command::trackClipboard: app.arrangeTrackClipboard(a, b); break;
    case Command::refToggle: app.toggleArrangeReference(); break;
    case Command::refLoad: app.openArrangeReference(); break;
    case Command::refDelete: app.deleteArrangeReference(); break;
    case Command::refB: app.toggleArrangeReferenceB(); break;
    case Command::clipToArrange: app.sendClipToArrange(a, b); break;
    case Command::carry: {
        const int length = app.arrangeCarryLength(a, b);
        if (length > 0) app.addArrangeBlock(a, b, c, d != 0, length);
        break;
    }
    }
}

FN(jintArray, arrangeData)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& app = jniApp();
    if (!app.arrangeView()) return jniInts(env, {0, int(app.arrangeRecordArmed()), int(app.arrangeRecording())});
    const auto& p = app.project();
    const auto& a = p.arrange;
    const auto selected = app.arrangeSelection();
    std::vector<jint> values{1, a.grid, int(a.snap), int(a.hideSteps), int(app.arrangeAuto()),
        int(a.loop), a.loopFrom, a.loopTo, p.arrangeEnd(), selected ? selected->first : -1,
        selected ? selected->second : -1, jint(p.tracks.size()), jint(a.markers.size()),
        int(app.hasArrangeBlockCopy()), int(app.hasArrangeTrackCopy()), int(app.arrangeRecording()),
        int(a.showRef && a.ref.has_value()), int(app.arrangeReferenceB()), int(app.arrangePlaying())};
    for (const auto& marker : a.markers)
        values.insert(values.end(), {marker.at, marker.end.value_or(-1), marker.color});
    for (int t = 0; t < int(p.tracks.size()); ++t) {
        const auto& track = p.tracks[size_t(t)];
        values.insert(values.end(), {track.color, int(track.mute), int(track.solo), int(app.trackStereo(t)), jint(track.blocks.size())});
        for (const auto& block : track.blocks) {
            const auto* clip = block.pattern ? &*block.pattern :
                (track.clipFilled(block.clip) ? &*track.clips[size_t(block.clip)] : nullptr);
            const int length = clip ? clip->playLen() : 0;
            values.insert(values.end(), {block.start, block.length, block.clip, block.offset,
                clip ? clip->passTicks() : 1, clip ? clip->speed() : 0, length});
            for (int i = 0; i < length; ++i) {
                const int at = clip->playIndex(i);
                const auto* step = clip->step(at);
                values.push_back(step && step->on && !clip->outside(at) ? step->len : 0);
            }
        }
    }
    return jniInts(env, values);
}

FN(jdouble, arrangePosition)(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    return jniApp().arrangePositionX();
}

FN(jintArray, arrangeTake)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    std::vector<jint> values;
    for (const auto& seg : jniApp().arrangeRecordedSegments()) {
        if (seg.slot >= 0 && !seg.silent)
            values.insert(values.end(), {seg.track, seg.start, seg.end});
    }
    return jniInts(env, values);
}

FN(jintArray, arrangeGroup)(JNIEnv* env, jobject, jint command, jint delta, jintArray data) {
    if (!data) return jniInts(env, {});
    const auto count = env->GetArrayLength(data);
    if (count % 2 != 0 || count > 16384) return jniInts(env, {});
    std::vector<jint> values(size_t(count), 0);
    if (count) env->GetIntArrayRegion(data, 0, count, values.data());
    if (env->ExceptionCheck()) return nullptr;
    std::vector<jambo::Project::BlockRef> group;
    for (int i = 0; i < count; i += 2) group.emplace_back(values[size_t(i)], values[size_t(i + 1)]);
    std::lock_guard<std::mutex> lock(jniLock());
    const auto next = jniApp().arrangeGroup(group, command, delta);
    values.clear();
    for (const auto& [track, block] : next) values.insert(values.end(), {track, block});
    return jniInts(env, values);
}

FN(jobjectArray, arrangeSections)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    std::vector<std::string> names;
    for (const auto& marker : jniApp().project().arrange.markers) names.push_back(marker.name);
    return jniStrings(env, names);
}

FN(jobjectArray, arrangeRefName)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& ref = jniApp().project().arrange.ref;
    return ref ? jniStrings(env, {ref->key, ref->name}) : jniStrings(env, {});
}

FN(jdoubleArray, arrangeRefInfo)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& ref = jniApp().project().arrange.ref;
    const jdouble data[] = {ref ? ref->offset : 0, ref ? ref->bpm.value_or(0) : 0};
    const auto array = env->NewDoubleArray(2);
    if (array) env->SetDoubleArrayRegion(array, 0, 2, data);
    return array;
}

FN(jfloatArray, arrangeRefPeaks)(JNIEnv* env, jobject, jstring name) {
    const auto key = jniString(env, name);
    jambo::SampleRef sample;
    {
        std::lock_guard<std::mutex> lock(jniLock());
        sample = jniApp().sampleData(key);
    }
    std::vector<jfloat> peaks;
    if (sample) {
        constexpr int hop = 256;
        for (int i = 0; i < sample->frames(); i += hop) {
            float peak = 0;
            for (int j = i; j < std::min(i + hop, sample->frames()); ++j)
                peak = std::max({peak, std::abs(sample->left()[j]), std::abs(sample->right()[j])});
            peaks.push_back(peak);
        }
    }
    const auto array = env->NewFloatArray(jsize(peaks.size()));
    if (array && !peaks.empty()) env->SetFloatArrayRegion(array, 0, jsize(peaks.size()), peaks.data());
    return array;
}

FN(void, arrangeRefOffset)(JNIEnv*, jobject, jdouble offset) {
    std::lock_guard<std::mutex> lock(jniLock());
    jniApp().setArrangeReferenceOffset(offset);
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

FN(jint, arrangeCarryLength)(JNIEnv*, jobject, jint track, jint clip) {
    std::lock_guard<std::mutex> lock(jniLock());
    return jniApp().arrangeCarryLength(track, clip);
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
        for (const auto& group : p.tracks[size_t(track)].autoGroups(id)) {
            if (!values.empty()) values.insert(values.end(), {std::numeric_limits<double>::quiet_NaN(), 0.0});
            for (const auto& pt : group) values.insert(values.end(), {pt.x, pt.y});
        }
    }
    const auto array = env->NewDoubleArray(jsize(values.size()));
    if (array && !values.empty()) env->SetDoubleArrayRegion(array, 0, jsize(values.size()), values.data());
    return array;
}

FN(jdouble, arrangeAutoBase)(JNIEnv* env, jobject, jint track, jstring lane) {
    const auto id = jniString(env, lane);
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& p = jniApp().project();
    if (track < 0 || track >= int(p.tracks.size())) return 0.5;
    const auto& t = p.tracks[size_t(track)];
    const auto& grid = jambo::Params::grid(t.sample);
    const auto item = std::find_if(grid.begin(), grid.end(), [&](const auto& def) { return def.id == id; });
    const auto* def = item != grid.end() ? &*item : jambo::Params::byId(id);
    if (!def) return 0.5;
    const auto it = t.params.find(id);
    return def->toNorm(it == t.params.end() ? def->def : it->second);
}

FN(void, arrangeSetAuto)(JNIEnv* env, jobject, jint track, jstring lane, jdoubleArray data, jboolean draw) {
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
    jniApp().setArrangeAuto(track, id, points, draw);
}

FN(void, arrangeDeleteAuto)(JNIEnv* env, jobject, jint track, jstring lane) {
    const auto id = lane ? jniString(env, lane) : std::string{};
    std::lock_guard<std::mutex> lock(jniLock());
    if (lane) jniApp().deleteArrangeAuto(track, id);
    else jniApp().clearArrangeAuto(track);
}

namespace {
jambo::GridSelection readGridSelection(JNIEnv* env, jintArray encoded) {
    jambo::GridSelection result;
    if (!encoded) return result;
    const auto count = env->GetArrayLength(encoded);
    if (count % 2 || count > 32768) return result;
    std::vector<jint> pairs(size_t(count), 0);
    if (count) env->GetIntArrayRegion(encoded, 0, count, pairs.data());
    if (env->ExceptionCheck()) return {};
    for (int i = 0; i < count; i += 2) {
        const int row = pairs[size_t(i)], track = pairs[size_t(i + 1)];
        if (row < 0) continue;
        if (track == -1) result.rows.insert(row);
        else if (track >= 0) result.cells[row].insert(track);
    }
    return result;
}
}

FN(jintArray, gridSelectionMasks)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& p = jniApp().project();
    std::vector<jint> masks(size_t(p.visibleRows()), 0);
    for (size_t row = 0; row < masks.size(); ++row)
        for (size_t track = 0; track < p.tracks.size(); ++track)
            if (p.tracks[track].clipFilled(int(row))) masks[row] |= jint(1u << track);
    return jniInts(env, masks);
}

FN(jintArray, arrangeSelectionPreview)(JNIEnv* env, jobject, jintArray selection, jint step) {
    const auto selected = readGridSelection(env, selection);
    if (env->ExceptionCheck()) return nullptr;
    std::lock_guard<std::mutex> lock(jniLock());
    const auto preview = jniApp().previewGridSelection(selected, step);
    return jniInts(env, {preview.begin(), preview.end()});
}

FN(void, arrangeSendSelection)(JNIEnv* env, jobject, jintArray selection, jint step, jint mode) {
    const auto selected = readGridSelection(env, selection);
    if (env->ExceptionCheck() || mode < 0 || mode > 2) return;
    std::lock_guard<std::mutex> lock(jniLock());
    jniApp().sendSelectionToArrange(selected, step, mode);
}

FN(void, setAdditionalSettings)(JNIEnv*, jobject, jboolean repeats, jboolean recordFromStart,
                                jboolean stretchBeat, jint sends, jint click) {
    std::lock_guard<std::mutex> lock(jniLock());
    jniApp().setAdditionalSettings(repeats, recordFromStart, stretchBeat, sends, click);
}

FN(jboolean, paintDrumSteps)(JNIEnv*, jobject, jint first, jint last) {
    std::lock_guard<std::mutex> lock(jniLock());
    return jniApp().paintDrumSteps(first, last);
}

// Заголовки ARRANGE: громкость, две полосы с пиками, sidechain и слышимость.
FN(jfloatArray, arrangeHeaderMeters)(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(jniLock());
    const auto& app = jniApp();
    const auto& p = app.project();
    std::vector<jfloat> values;
    for (int t = 0; t <= int(p.tracks.size()); ++t) {
        const bool ref = t == int(p.tracks.size());
        const int channel = ref ? jambo::Levels::ref : t;
        const auto bars = app.meters().bars(channel, true);
        const auto left = bars.empty() ? jambo::MeterBar{} : bars[0];
        const auto right = bars.size() < 2 ? left : bars[1];
        values.insert(values.end(), {jfloat(app.meterVol(channel, false)), left.level, left.hold, right.level, right.hold,
            ref ? 0.f : app.meters().duckDb(t), ref || p.audible(t) ? 1.f : 0.f});
    }
    auto array = env->NewFloatArray(jsize(values.size()));
    if (array) env->SetFloatArrayRegion(array, 0, jsize(values.size()), values.data());
    return array;
}

FN(void, setArrangeHeaderVolume)(JNIEnv*, jobject, jint track, jdouble db) {
    if (!std::isfinite(db)) return;
    std::lock_guard<std::mutex> lock(jniLock());
    jniApp().setMeterVol(track == -1 ? jambo::Levels::ref : track, db, false);
}
