// Запись проекта в JSON (порт encode(to:) ProjectCoding.swift и других файлов Model, ветка swift JAMbo): те же ключи и
// тот же порядок; необязательные — только если есть (encodeIfPresent).
#include "Json.h"
#include "KnobDefaults.h"
#include "Project.h"
#include "../midi/TrackMidi.h"

namespace jambo {
namespace json {

Json put(const ModAssign& v) {
    Json o = Json::object();
    o["lfo"] = put(v.lfo);
    o["amount"] = put(v.amount);
    putIf(o, "track", v.track);
    return o;
}

Json put(const Step& v) {
    Json o = Json::object();
    o["on"] = put(v.on);
    o["len"] = put(v.len);
    o["notes"] = put(v.notes);
    putIf(o, "lens", v.lens);
    putIf(o, "vels", v.vels);
    o["locks"] = put(v.locks);
    putIf(o, "sound", v.sound);
    putIf(o, "soundLocks", v.soundLocks);
    return o;
}

Json put(const Clip& v) {
    Json o = Json::object();
    o["pages"] = put(v.pages);
    o["length"] = put(v.length);
    o["playFrom"] = put(v.playFrom);
    o["playTo"] = put(v.playTo);
    putIf(o, "scale", v.scale);
    putIf(o, "root", v.root);
    putIf(o, "fx", v.fx);
    putIf(o, "src", v.src);
    putIf(o, "rate", v.rate);
    return o;
}

Json put(const SceneRow& v) {
    Json o = Json::object();
    o["name"] = put(v.name);
    o["repeats"] = put(v.repeats);
    return o;
}

Json put(const Track& v) {
    Json o = Json::object();
    o["id"] = put(v.id);
    o["name"] = put(v.name);
    o["sample"] = put(v.sample);
    o["params"] = put(v.params);
    o["clips"] = put(v.clips);
    o["slot"] = put(v.slot);
    o["open"] = put(v.open);
    o["mute"] = put(v.mute);
    o["solo"] = put(v.solo);
    o["stopped"] = put(v.stopped);
    o["folder"] = put(v.folder);
    o["slices"] = put(v.slices);
    o["vol"] = put(v.vol);
    o["trim"] = put(v.trim);
    o["table"] = put(v.table);
    o["grain"] = put(v.grain);
    o["assigns"] = put(v.assigns);
    o["morphOrig"] = put(v.morphOrig);
    o["morphOrigAssign"] = put(v.morphOrigAssign);
    o["morphEdited"] = put(v.morphEdited);
    o["eq"] = put(v.eq);
    putIf(o, "home", v.home);
    o["midi"] = put(v.midi);
    o["blocks"] = put(v.blocks);
    o["color"] = put(v.color);
    o["autos"] = put(v.autos);
    o["warps"] = put(v.warps);
    o["drumStyle"] = put(v.drumStyle);
    o["arp"] = put(v.arp);
    o["keyOctave"] = put(v.keyOctave);
    return o;
}

Json put(const Project& v) {
    Json o = Json::object();
    o["version"] = put(v.version);
    o["bpm"] = put(v.bpm);
    o["scale"] = put(v.scale);
    o["root"] = put(v.root);
    o["fx"] = put(v.fx);
    o["fxAssigns"] = put(v.fxAssigns);
    o["master"] = put(v.master);
    o["satBypass"] = put(v.satBypass);
    o["cmpBypass"] = put(v.cmpBypass);
    o["clipBypass"] = put(v.clipBypass);
    o["limBypass"] = put(v.limBypass);
    o["outVol"] = put(v.outVol);
    o["masterEq"] = put(v.masterEq);
    o["tracks"] = put(v.tracks);
    o["chanSeqs"] = put(v.chanSeqs);
    o["scenes"] = put(v.scenes);
    o["rows"] = put(v.rows);
    o["arrStart"] = put(v.arrStart);
    o["arrEndStop"] = put(v.arrEndStop);
    o["plusCopy"] = put(v.plusCopy);
    o["files"] = put(v.files);
    o["roots"] = put(v.roots);
    o["rowLoop"] = put(v.rowLoop);
    o["arrowSwitch"] = put(v.arrowSwitch);
    o["prevRow"] = put(v.prevRow);
    putIf(o, "notice", v.notice);
    o["arrange"] = put(v.arrange);
    o["gridHideSteps"] = put(v.gridHideSteps);
    return o;
}

Json put(const EqBand& v) {
    Json o = Json::object();
    o["on"] = put(v.on);
    o["type"] = put(v.type);
    o["freq"] = put(v.freq);
    o["gain"] = put(v.gain);
    o["q"] = put(v.q);
    o["slope"] = put(v.slope);
    o["ms"] = put(v.ms);
    o["dyn"] = put(v.dyn);
    o["thr"] = put(v.thr);
    o["range"] = put(v.range);
    return o;
}

Json put(const Eq& v) {
    Json o = Json::object();
    o["on"] = put(v.on);
    o["bands"] = put(v.bands);
    return o;
}

Json put(const Folder& v) {
    Json o = Json::object();
    o["items"] = put(v.items);
    o["index"] = put(v.index);
    return o;
}

Json put(const SliceMark& v) {
    Json o = Json::object();
    o["at"] = put(v.at);
    putIf(o, "note", v.note);
    putIf(o, "role", v.role);
    putIf(o, "free", v.free);
    return o;
}

Json put(const FolderItem& v) {
    Json o = Json::object();
    if (v.preset) o["preset"] = put(*v.preset);
    else o["sample"] = put(v.sample.value_or(""));
    return o;
}

Json put(const SynthPreset& v) {
    Json o = Json::object();
    o["name"] = put(v.name);
    o["sound"] = put(v.sound);
    o["params"] = put(v.params);
    putIf(o, "fmt", v.fmt);
    putIf(o, "table", v.table);
    putIf(o, "grain", v.grain);
    putIf(o, "mods", v.mods);
    putIf(o, "assigns", v.assigns);
    return o;
}

Json put(const WarpMark& v) {
    Json o = Json::object();
    o["at"] = put(v.at);
    o["beat"] = put(v.beat);
    return o;
}

Json put(const WarpInfo& v) {
    Json o = Json::object();
    putIf(o, "bpm", v.bpm);
    o["marks"] = put(v.marks);
    putIf(o, "off", v.off);
    return o;
}

Json put(const TrackMidi& v) {
    Json o = Json::object();
    o["inOn"] = put(v.inOn);
    if (v.inDevice) o["inDevice"] = Json(*v.inDevice);
    o["inName"] = put(v.inName);
    o["inChannel"] = put(v.inChannel);
    if (v.outDevice) o["outDevice"] = Json(*v.outDevice);
    o["outName"] = put(v.outName);
    o["outChannel"] = put(v.outChannel);
    return o;
}

Json put(const TrackArp& v) {
    Json o = Json::object();
    o["mode"] = put(v.mode);
    o["on"] = put(v.on);
    o["dir"] = put(v.dir);
    o["rate"] = put(v.rate);
    o["octaves"] = put(v.octaves);
    o["gate"] = put(v.gate);
    o["latch"] = put(v.latch);
    return o;
}

Json put(const ArrAutoPoint& v) {
    Json o = Json::object();
    o["x"] = put(v.x);
    o["y"] = put(v.y);
    return o;
}

Json put(const ArrAutoLane& v) {
    Json o = Json::object();
    o["param"] = put(v.param);
    o["points"] = put(v.points);
    return o;
}

/// legacyLink не пишется.
Json put(const ArrBlock& v) {
    Json o = Json::object();
    o["clip"] = put(v.clip);
    o["start"] = put(v.start);
    o["length"] = put(v.length);
    o["offset"] = put(v.offset);
    if (v.pattern) o["pattern"] = put(*v.pattern);
    o["autos"] = put(v.autos);
    return o;
}

Json put(const ArrMarker& v) {
    Json o = Json::object();
    o["at"] = put(v.at);
    o["name"] = put(v.name);
    o["color"] = put(v.color);
    putIf(o, "end", v.end);
    return o;
}

Json put(const ArrRef& v) {
    Json o = Json::object();
    o["key"] = put(v.key);
    o["name"] = put(v.name);
    o["offset"] = put(v.offset);
    o["gain"] = put(v.gain);
    putIf(o, "bpm", v.bpm);
    return o;
}

Json put(const Arrangement& v) {
    Json o = Json::object();
    o["markers"] = put(v.markers);
    o["loopFrom"] = put(v.loopFrom);
    o["loopTo"] = put(v.loopTo);
    o["loop"] = put(v.loop);
    o["grid"] = put(v.grid);
    o["snap"] = put(v.snap);
    o["hideSteps"] = put(v.hideSteps);
    putIf(o, "ref", v.ref);
    o["showRef"] = put(v.showRef);
    return o;
}

Json put(const TrackSound& v) {
    Json o = Json::object();
    o["sample"] = put(v.sample);
    o["params"] = put(v.params);
    o["folder"] = put(v.folder);
    o["slices"] = put(v.slices);
    putIf(o, "warps", v.warps);
    o["table"] = put(v.table);
    putIf(o, "grain", v.grain);
    o["assigns"] = put(v.assigns);
    o["eq"] = put(v.eq);
    putIf(o, "vol", v.vol);
    return o;
}

Json put(const KnobDefaults& v) {
    Json o = Json::object();
    o["values"] = put(v.values);
    putIf(o, "eqTrack", v.eqTrack);
    putIf(o, "eqMaster", v.eqMaster);
    putIf(o, "filterV", v.filterV);
    return o;
}

}  // namespace json
}  // namespace jambo
