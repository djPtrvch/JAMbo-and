// Чтение проекта из JSON (порт init(from:) ProjectCoding.swift и других файлов Model, ветка swift JAMbo): недостающие
// поля — значения по умолчанию, неизвестные пропускаются, поле не того типа — тоже по умолчанию (правила — Json.h).
#include "DrumRhythm.h"
#include "FilterType.h"
#include "Json.h"
#include "KnobDefaults.h"
#include "Project.h"
#include "SynthMigration.h"
#include "../midi/TrackMidi.h"

namespace jambo {
namespace json {

bool get(const Json& j, ModAssign& v) {
    if (!j.is_object()) return false;
    v.lfo = value(j, "lfo", 1);
    v.amount = value(j, "amount", 0.0);
    v.track = opt<int>(j, "track");
    return true;
}

bool get(const Json& j, Step& v) {
    if (!j.is_object()) return false;
    v.on = value(j, "on", false);
    v.len = value(j, "len", 1);
    v.notes = value(j, "notes", std::vector<int>());
    v.lens = opt<std::vector<int>>(j, "lens");
    v.vels = opt<std::vector<double>>(j, "vels");
    v.locks = value(j, "locks", ParamMap());
    v.sound = opt<std::string>(j, "sound");
    v.soundLocks = opt<std::vector<std::string>>(j, "soundLocks");
    return true;
}

bool get(const Json& j, Clip& v) {
    if (!j.is_object()) return false;
    v.pages = value(j, "pages", std::vector<Page>{emptyPage()});
    v.length = value(j, "length", Params::stepsPerPage);
    v.playFrom = value(j, "playFrom", 0);
    v.playTo = value(j, "playTo", 0);
    v.scale = opt<int>(j, "scale");
    v.root = opt<int>(j, "root");
    v.fx = opt<ParamMap>(j, "fx");
    v.src = opt<TrackSound>(j, "src");
    v.rate = opt<int>(j, "rate");
    return true;
}

bool get(const Json& j, SceneRow& v) {
    if (!j.is_object()) return false;
    v.name = value(j, "name", std::string());
    v.repeats = value(j, "repeats", 1);
    return true;
}

bool get(const Json& j, Track& v) {
    if (!j.is_object()) return false;
    v = Track(0);  // id — случайный, если в файле нет
    v.id = value(j, "id", v.id);
    v.name = value(j, "name", std::string());
    v.sample = value(j, "sample", std::string());
    v.params = value(j, "params", ParamMap());
    v.clips = value(j, "clips", Track::firstClips());
    v.slot = value(j, "slot", 0);
    v.open = value(j, "open", 0);
    v.mute = value(j, "mute", false);
    v.solo = value(j, "solo", false);
    v.stopped = value(j, "stopped", false);
    v.folder = value(j, "folder", Folder());
    v.slices = value(j, "slices", std::map<std::string, std::vector<SliceMark>>());
    v.warps = value(j, "warps", std::map<std::string, WarpInfo>());
    v.vol = value(j, "vol", Params::volDef);
    v.trim = value(j, "trim", 0.0);
    v.table = value(j, "table", std::string());
    v.grain = value(j, "grain", std::string());
    v.assigns = value(j, "assigns", std::map<std::string, ModAssign>());
    v.morphOrig = value(j, "morphOrig", ParamMap());
    v.morphOrigAssign = value(j, "morphOrigAssign", std::map<std::string, std::optional<ModAssign>>());
    v.morphEdited = value(j, "morphEdited", false);
    v.eq = value(j, "eq", Eq());
    v.home = opt<TrackSound>(j, "home");
    v.midi = value(j, "midi", TrackMidi());
    v.blocks = value(j, "blocks", std::vector<ArrBlock>());
    v.color = value(j, "color", int(ArrPalette::defaultColor));
    v.autos = value(j, "autos", std::vector<ArrAutoLane>());
    v.drumStyle = DrumRhythm::styleIndex(value(j, "drumStyle", 0));
    v.arp = value(j, "arp", TrackArp());
    v.keyOctave = value(j, "keyOctave", Scales::octaveDef);
    v.migrateAutos();
    v.dropLegacyLinks();
    return true;
}

bool get(const Json& j, Project& v) {
    if (!j.is_object()) return false;
    v = Project();
    v.tracks = value(j, "tracks", std::vector<Track>());
    v.version = value(j, "version", 2);
    v.bpm = value(j, "bpm", 120.0);
    v.scale = value(j, "scale", 0);
    v.root = value(j, "root", 0);
    v.fx = value(j, "fx", Params::fxDefaults());
    v.fxAssigns = value(j, "fxAssigns", std::map<std::string, ModAssign>());
    v.master = value(j, "master", MasterChain::defaultValues());
    v.satBypass = value(j, "satBypass", false);
    v.cmpBypass = value(j, "cmpBypass", false);
    v.clipBypass = value(j, "clipBypass", false);
    v.limBypass = value(j, "limBypass", false);
    v.outVol = value(j, "outVol", 0.0);
    v.masterEq = value(j, "masterEq", Eq());
    v.chanSeqs = value(j, "chanSeqs", std::vector<Track>{Track::channel("MIX"), Track::channel("MST")});
    v.scenes = value(j, "scenes", std::vector<SceneRow>(size_t(Params::patternSlots)));
    v.rows = value(j, "rows", 1);
    v.arrStart = value(j, "arrStart", 0);
    v.arrEndStop = value(j, "arrEndStop", false);
    v.plusCopy = value(j, "plusCopy", false);
    v.files = value(j, "files", std::map<std::string, std::string>());
    v.roots = value(j, "roots", std::map<std::string, int>());
    v.rowLoop = value(j, "rowLoop", true);
    v.arrowSwitch = value(j, "arrowSwitch", false);
    v.prevRow = value(j, "prevRow", false);
    v.notice = opt<std::string>(j, "notice");
    v.arrange = value(j, "arrange", Arrangement());
    v.gridHideSteps = value(j, "gridHideSteps", false);
    return true;
}

bool get(const Json& j, EqBand& v) {
    if (!j.is_object()) return false;
    v.on = value(j, "on", true);
    v.type = value(j, "type", int(EqSpec::bell));
    v.freq = value(j, "freq", 1000.0);
    v.gain = value(j, "gain", 0.0);
    v.q = value(j, "q", 0.71);
    v.slope = value(j, "slope", 0);
    v.ms = value(j, "ms", 0);
    v.dyn = value(j, "dyn", -1);
    v.thr = value(j, "thr", EqSpec::dynThrDef);
    v.range = value(j, "range", EqSpec::dynRangeDef);
    return true;
}

bool get(const Json& j, Eq& v) {
    if (!j.is_object()) return false;
    v.on = value(j, "on", true);
    v.bands = value(j, "bands", std::vector<std::optional<EqBand>>(size_t(EqSpec::bands)));
    // Сохранённые до 10 полос (8 мест) — дополнить пустыми.
    v.bands.resize(size_t(EqSpec::bands));
    return true;
}

bool get(const Json& j, Folder& v) {
    if (!j.is_object()) return false;
    v.items = value(j, "items", std::vector<FolderItem>());
    v.index = value(j, "index", -1);
    return true;
}

bool get(const Json& j, SliceMark& v) {
    if (!j.is_object()) return false;
    v.at = value(j, "at", 0.0);
    v.note = opt<int>(j, "note");
    v.role = opt<int>(j, "role");
    v.free = opt<bool>(j, "free");
    return true;
}

bool get(const Json& j, FolderItem& v) {
    // Прежние файлы — строкой (ключ сэмпла или «mi:<n>»).
    if (j.is_string()) {
        v = FolderItem::ofSample(j.get<std::string>());
        return true;
    }
    if (!j.is_object()) return false;
    if (auto p = opt<SynthPreset>(j, "preset")) v = FolderItem::ofPreset(*p);
    else v = FolderItem::ofSample(value(j, "sample", std::string()));
    return true;
}

bool get(const Json& j, SynthPreset& v) {
    if (!j.is_object()) return false;
    v.name = value(j, "name", std::string());
    v.sound = value(j, "sound", std::string());
    v.params = value(j, "params", ParamMap());
    v.fmt = opt<int>(j, "fmt");
    // fmt < 3 — ручки своих движков (1.4); < 4 — ручки GRANULAR; < 5 — прежняя двухполярная ручка Filter.
    if (v.fmt.value_or(1) < 3) {
        SynthMigration::params(v.sound, v.params);
        v.fmt = 3;
    }
    if (v.fmt.value_or(1) < 4) {
        GranularMigration::params(v.sound, v.params);
        v.fmt = 4;
    }
    if (v.fmt.value_or(1) < 5) {
        FilterMigration::preset(v.params);
        v.fmt = 5;
    }
    v.table = opt<std::string>(j, "table");
    v.grain = opt<std::string>(j, "grain");
    v.mods = opt<ParamMap>(j, "mods");
    v.assigns = opt<std::map<std::string, ModAssign>>(j, "assigns");
    return true;
}

bool get(const Json& j, WarpMark& v) {
    return j.is_object() && req(j, "at", v.at) && req(j, "beat", v.beat);
}

bool get(const Json& j, WarpInfo& v) {
    return j.is_object() && ifPresent(j, "bpm", v.bpm) && req(j, "marks", v.marks) && ifPresent(j, "off", v.off);
}

bool get(const Json& j, TrackMidi& v) {
    if (!j.is_object()) return false;
    v.inOn = value(j, "inOn", false);
    v.inDevice = optInt32(j, "inDevice");
    v.inName = value(j, "inName", std::string());
    const int ic = value(j, "inChannel", 0);
    v.inChannel = ic >= 0 && ic <= 16 ? ic : 0;
    v.outDevice = optInt32(j, "outDevice");
    v.outName = value(j, "outName", std::string());
    const int oc = value(j, "outChannel", 1);
    v.outChannel = oc >= 1 && oc <= 16 ? oc : 1;
    return true;
}

bool get(const Json& j, TrackArp& v) {
    if (!j.is_object()) return false;
    v.mode = value(j, "mode", false);
    v.on = value(j, "on", false);
    v.dir = Arp::clampDir(value(j, "dir", 0));
    v.rate = Arp::clampRate(value(j, "rate", 2));
    v.octaves = Arp::clampOctaves(value(j, "octaves", 1));
    v.gate = Arp::clampGate(value(j, "gate", 0.5));
    v.latch = value(j, "latch", false);
    return true;
}

bool get(const Json& j, ArrAutoPoint& v) { return j.is_object() && req(j, "x", v.x) && req(j, "y", v.y); }

bool get(const Json& j, ArrAutoLane& v) {
    return j.is_object() && req(j, "param", v.param) && req(j, "points", v.points);
}

bool get(const Json& j, ArrBlock& v) {
    if (!j.is_object()) return false;
    v.clip = value(j, "clip", 0);
    v.start = value(j, "start", 0);
    v.length = value(j, "length", ArrGrid::stepsPerBar);
    v.offset = value(j, "offset", 0);
    v.pattern = opt<Clip>(j, "pattern");
    v.autos = value(j, "autos", std::vector<ArrAutoLane>());
    v.legacyLink = value(j, "autoLink", false);
    return true;
}

bool get(const Json& j, ArrMarker& v) {
    if (!j.is_object()) return false;
    v.at = value(j, "at", 0);
    v.name = value(j, "name", std::string());
    v.color = value(j, "color", int(ArrPalette::defaultColor));
    v.end = opt<int>(j, "end");
    return true;
}

bool get(const Json& j, ArrRef& v) {
    return j.is_object() && req(j, "key", v.key) && req(j, "name", v.name) && req(j, "offset", v.offset) &&
           req(j, "gain", v.gain) && ifPresent(j, "bpm", v.bpm);
}

bool get(const Json& j, Arrangement& v) {
    if (!j.is_object()) return false;
    v.markers = value(j, "markers", std::vector<ArrMarker>());
    v.loopFrom = value(j, "loopFrom", 0);
    v.loopTo = value(j, "loopTo", 0);
    v.loop = value(j, "loop", false);
    v.grid = value(j, "grid", 2);
    v.snap = value(j, "snap", true);
    v.hideSteps = value(j, "hideSteps", false);
    v.ref = opt<ArrRef>(j, "ref");
    v.showRef = value(j, "showRef", false);
    return true;
}

bool get(const Json& j, TrackSound& v) {
    return j.is_object() && req(j, "sample", v.sample) && req(j, "params", v.params) && req(j, "folder", v.folder) &&
           req(j, "slices", v.slices) && ifPresent(j, "warps", v.warps) && req(j, "table", v.table) &&
           ifPresent(j, "grain", v.grain) && req(j, "assigns", v.assigns) && req(j, "eq", v.eq) &&
           ifPresent(j, "vol", v.vol);
}

bool get(const Json& j, KnobDefaults& v) {
    return j.is_object() && req(j, "values", v.values) && ifPresent(j, "eqTrack", v.eqTrack) &&
           ifPresent(j, "eqMaster", v.eqMaster) && ifPresent(j, "filterV", v.filterV);
}

}  // namespace json
}  // namespace jambo
