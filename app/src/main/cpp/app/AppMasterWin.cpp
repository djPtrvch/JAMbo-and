// Окна модулей мастера (порт AppModelMaster.swift и AppModelDefaults.saveMasterDefault, ветка swift JAMbo): SAT, COMP,
// CLIP, LIM на месте центра при MST; PATTERNS, EQ и браузер не открыты одновременно с ними. Поля, тапы по RATIO,
// TYPE, RELEASE, AUTO MAKEUP, UNITY, пресеты модулей и SAVE DEFAULT.
#include "App.h"
#include <fstream>
#include <sstream>
#include "master/Glue.h"
#include "master/Clipper.h"
#include "model/MasterInput.h"
#include "model/ProjectStore.h"

namespace jambo {

static std::string readText(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void writeText(const std::string& p, const std::string& data) {
    if (p.empty()) return;
    const std::string tmp = p + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << data;
    }
    std::rename(tmp.c_str(), p.c_str());
}

void App::loadAppFiles() {
    masterPresets_ = MasterPresetBook::decode(readText(masterPresetsPath_));
    const std::string kd = readText(knobDefaultsPath_);
    if (!kd.empty()) knobDefaults_ = ProjectCoding::decodeKnobDefaults(kd);
}

void App::openMasterWindow(int m) {
    if (channel_ != Channel::master || m < 0 || m > 3) return;
    masterWin_ = m;
    setUnity(false);
    browser_.reset();
    eqView_ = false;
    patternView_ = false;
    bump();
}

void App::tapChannel(Channel c) {
    const bool again = mixTapAt_ >= 0 && now_ - mixTapAt_ < 0.35 && channel_ == c;
    if (again && c == Channel::master && masterWin_) {
        mixTapAt_ = -1;
        closeMasterWindow();
        return;
    }
    if (again && c == Channel::mix && mixerOpen_) {
        mixTapAt_ = -1;
        setMixerOpen(false);
        return;
    }
    mixTapAt_ = c == Channel::track ? -1 : now_;
    selectChannel(c);
}

void App::setMixerOpen(bool on) {
    if (on && channel_ != Channel::mix) return;
    mixerOpen_ = on;
    if (on) {
        browser_.reset();
        eqView_ = false;
        patternView_ = false;
    }
    bump();
}

void App::closeMasterWindow() {
    if (!masterWin_) return;
    masterWin_.reset();
    setUnity(false);
    bump();
}

void App::setUnity(bool on) {
    const int m = masterWin_.value_or(-1);
    const bool ok = on && (m == 2 || m == 3);
    masterUnity_ = ok;
    if (MasterChain* mc = audio_->master()) mc->unity.store(ok ? m + 1 : 0);
    bump();
}

std::vector<const ParamDef*> App::masterFields(int m) {
    std::vector<const ParamDef*> out;
    for (const auto& id : MasterPresetBook::ids(m))
        if (const ParamDef* p = Params::byId(id)) out.push_back(p);
    return out;
}

void App::setMaster(const ParamDef& p, double v) {
    const double c = MasterInput::clamp(p, v);
    edit([&](Project& pr) { pr.master[p.id] = c; });
}

void App::tapRatio() {
    const ParamDef* p = Params::byId("cmp.ratio");
    if (!p) return;
    const double next = MasterGlue::nextRatio(masterParam(*p));
    edit([&](Project& pr) { pr.master[p->id] = next; });
}

void App::tapSatType() {
    const ParamDef& p = Params::masterColumns()[0][1];
    const double next = double((int(std::round(masterParam(p))) + 1) % 3);
    edit([&](Project& pr) { pr.master[p.id] = next; });
}

void App::tapRelease(const ParamDef& p) {
    const double v = masterParam(p);
    double next;
    if (v >= MasterChain::autoValue) {
        const auto it = releaseManual_.find(p.id);
        next = it == releaseManual_.end() ? p.def : it->second;
    } else {
        releaseManual_[p.id] = v;
        next = MasterChain::autoValue;
    }
    edit([&](Project& pr) { pr.master[p.id] = next; });
}

bool App::makeupLive() const {
    const ParamDef* p = Params::byId("cmp.makeup");
    return makeupState_ == 2 && p && std::fabs(masterParam(*p) - makeupApplied_) < 1e-6;
}

void App::tapMakeup() {
    const ParamDef* p = Params::byId("cmp.makeup");
    MasterChain* chain = audio_->master();
    if (!p || !chain) return;
    if (makeupState_ == 1) {
        chain->measureGen.store(0);
        makeupState_ = 0;
        bump();
        return;
    }
    if (makeupLive()) {
        const double back = makeupManual_.value_or(p->def);
        makeupState_ = 0;
        edit([&](Project& pr) { pr.master[p->id] = back; });
        return;
    }
    makeupManual_ = masterParam(*p);
    makeupRun_ += 1;
    chain->measureIn.store(-240);
    chain->measureOut.store(-240);
    chain->measureGen.store(makeupRun_);
    makeupState_ = 1;
    makeupDue_ = now_ + 4;
    bump();
}

/// Замер AUTO MAKEUP: через 4 с — MAKEUP на разницу громкости входа и выхода; тише −50 dB — NO SIGNAL на 2 с.
void App::tickMakeup() {
    if (makeupState_ == 3 && now_ >= makeupDue_) {
        makeupState_ = 0;
        bump();
        return;
    }
    if (makeupState_ != 1 || now_ < makeupDue_) return;
    MasterChain* chain = audio_->master();
    const ParamDef* p = Params::byId("cmp.makeup");
    if (!chain || !p) return;
    chain->measureGen.store(0);
    const double inDb = chain->measureIn.load(), outDb = chain->measureOut.load();
    if (inDb < -50) {
        makeupState_ = 3;
        makeupDue_ = now_ + 2;
        bump();
        return;
    }
    const double mk = MasterInput::clamp(*p, std::round((inDb - outDb) * 10) / 10);
    edit([&](Project& pr) { pr.master[p->id] = mk; });
    makeupApplied_ = mk;
    makeupState_ = 2;
}

std::string App::makeupLabel(const std::string& plain) const {
    if (makeupState_ == 1) return "LISTEN…";
    if (makeupState_ == 3) return "NO SIGNAL";
    return makeupLive() ? "AUTO " + plain : plain;
}

void App::saveMasterPreset(int m, const std::string& name, std::optional<int> over) {
    masterPresets_.save(m, name, project_.master, over);
    writeText(masterPresetsPath_, masterPresets_.encode());
    bump();
}

void App::loadMasterPreset(int m, int k) {
    const MasterPresetBook book = masterPresets_;
    edit([&](Project& pr) { book.apply(m, k, pr.master); });
}

void App::renameMasterPreset(int m, int k, const std::string& name) {
    masterPresets_.rename(m, k, name);
    writeText(masterPresetsPath_, masterPresets_.encode());
    bump();
}

void App::deleteMasterPreset(int m, int k) {
    masterPresets_.remove(m, k);
    writeText(masterPresetsPath_, masterPresets_.encode());
    bump();
}

void App::moveMasterPreset(int m, int from, int to) {
    masterPresets_.move(m, from, to);
    writeText(masterPresetsPath_, masterPresets_.encode());
    bump();
}

void App::saveMasterDefault(int m) {
    const ParamMap defs = MasterChain::defaultValues();
    for (const auto& id : MasterPresetBook::ids(m)) {
        const auto a = project_.master.find(id);
        const auto d = defs.find(id);
        knobDefaults_.values[KnobDefaults::key("mst", id)] = a != project_.master.end() ? a->second : d != defs.end() ? d->second : 0;
    }
    writeText(knobDefaultsPath_, ProjectCoding::encode(knobDefaults_));
    defaultFlash_ = m;
    defaultFlashUntil_ = now_ + 1;
    bump();
}

}  // namespace jambo
