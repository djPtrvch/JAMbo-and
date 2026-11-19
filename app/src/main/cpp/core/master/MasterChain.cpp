// Цепочка мастера (порт Core/Master/MasterChain.swift).
#include "MasterChain.h"
#include <cmath>
#include "Clipper.h"
#include "Glue.h"
#include "Limiter.h"
#include "Saturator.h"
#include "eq/EqDyn.h"
#include "eq/EqListen.h"
#include "eq/EqModel.h"
#include "eq/EqUnit.h"
#include "model/Project.h"
#include "util/RenderProfile.h"

namespace jambo {

const std::vector<std::pair<std::string, int>>& MasterChain::ids() {
    static const std::vector<std::pair<std::string, int>> v = {
        {"sat.drive", Index::satDrive}, {"sat.type", Index::satType}, {"sat.tone", Index::satTone},
        {"sat.mix", Index::satMix}, {"sat.focus", Index::satFocus},
        {"cmp.threshold", Index::cmpThr}, {"cmp.attack", Index::cmpAtt}, {"cmp.release", Index::cmpRel},
        {"cmp.makeup", Index::cmpMakeup}, {"cmp.ratio", Index::cmpRatio}, {"cmp.knee", Index::cmpKnee},
        {"cmp.mix", Index::cmpMix}, {"cmp.schpf", Index::cmpSchpf},
        {"clip.drive", Index::clipDrive}, {"clip.ceiling", Index::clipCeiling},
        {"clip.softness", Index::clipSoftness}, {"clip.bass", Index::clipBass},
        {"lim.input", Index::limGain}, {"lim.ceiling", Index::limCeiling}, {"lim.lookahead", Index::limLookahead},
        {"lim.release", Index::limRel}, {"lim.guard", Index::limGuard},
    };
    return v;
}

ParamMap MasterChain::defaultValues() {
    ParamMap d;
    for (const auto& p : ids()) d[p.first] = defaults[p.second];
    d["cmp.attack"] = newAttack;
    return d;
}

void MasterChain::fill(double* p, const Project& project) {
    for (int k = 0; k < Index::count; ++k) p[k] = defaults[k];
    for (const auto& [id, k] : ids()) {
        auto it = project.master.find(id);
        if (it != project.master.end()) p[k] = it->second;
    }
    const int pairs[2][2] = {{Index::cmpRel, Index::cmpAuto}, {Index::limRel, Index::limAuto}};
    for (const auto& ra : pairs) {
        if (p[ra[0]] >= autoValue) {
            p[ra[0]] = autoRel;
            p[ra[1]] = 1;
        }
    }
    p[Index::satBypass] = project.satBypass ? 1 : 0;
    p[Index::cmpBypass] = project.cmpBypass ? 1 : 0;
    p[Index::clipBypass] = project.clipBypass ? 1 : 0;
    p[Index::limBypass] = project.limBypass ? 1 : 0;
    p[Index::outGain] = Params::volGain(project.outVol);
}

MasterChain::MasterChain(double sampleRate)
    : sat(std::make_unique<MasterSaturator>(sampleRate)), glue(std::make_unique<MasterGlue>(sampleRate)),
      clip(std::make_unique<MasterClipper>(sampleRate)), lim(std::make_unique<MasterLimiter>(sampleRate)),
      eq(std::make_unique<EqUnit>(sampleRate)), outK(float(1 - std::exp(-1 / (0.01 * sampleRate)))),
      unityK(float(1 - std::exp(-1 / (0.02 * sampleRate)))) {}

MasterChain::~MasterChain() = default;

void MasterChain::process(float* left, float* right, int n, const double* p, const double* e) {
    using I = Index;
    if (e) {
        const bool shown = spectra && spectra->channel.load() == EqSpec::masterChannel;
        if (shown) spectra->pre.push(left, right, n);
        eq->process(left, right, n, e);
        if (eqDyn) eqDyn->publish(EqSpec::masterChannel, *eq);
        if (shown) spectra->post.push(left, right, n);
        if (eqListen) eqListen->process(EqSpec::masterChannel, *eq, n);
        if (profile) profile->lap(RenderProfile::eq);
    }
    const bool satShown = satOn.load() != 0;
    if (satShown) satIn.push(left, right, n);
    sat->process(left, right, left, right, n, p[I::satDrive], p[I::satType], p[I::satTone],
                 p[I::satMix] * (1 - p[I::satBypass]), p[I::satFocus]);
    if (satShown) satOut.push(left, right, n);
    const long sm = scope.mask.load();
    const long un = unity.load();
    scope.tap(MasterScope::sat, sm, left, right, n);
    const long mg = measureGen.load();
    if (mg != measureSeen) {
        measureSeen = mg;
        glue->resetMeasure();
    }
    glue->measuring = mg > 0;
    glue->process(left, right, left, right, n, p[I::cmpThr], p[I::cmpAtt], p[I::cmpRel], p[I::cmpAuto] >= 0.5,
                  p[I::cmpMakeup], p[I::cmpRatio], p[I::cmpKnee], p[I::cmpMix], p[I::cmpSchpf], p[I::cmpBypass]);
    if (mg > 0 && glue->kCount > 0) {
        const double c = double(glue->kCount);
        measureIn.store(float(10 * std::log10(glue->kSumIn / c + 1e-30)));
        measureOut.store(float(10 * std::log10(glue->kSumOut / c + 1e-30)));
    }
    scope.tap(MasterScope::comp, sm, left, right, n);
    clip->process(left, right, left, right, n, p[I::clipDrive], p[I::clipCeiling], p[I::clipSoftness], p[I::clipBass],
                  p[I::clipBypass]);
    scope.tap(MasterScope::clip, sm, left, right, n);
    lim->process(left, right, left, right, n, p[I::limGain], p[I::limCeiling], p[I::limLookahead], p[I::limRel],
                 p[I::limAuto] >= 0.5, p[I::limBypass], p[I::limGuard]);
    limGuardCut.store(float(lim->guardCut));
    double ut = 1;
    if (un == MasterScope::clip + 1 && p[I::clipBypass] < 0.5) ut = std::pow(10.0, -p[I::clipDrive] / 20);
    if (un == MasterScope::lim + 1 && p[I::limBypass] < 0.5) ut = std::pow(10.0, -(p[I::limGain] - lim->guardCut) / 20);
    unityTarget = float(ut);
    scope.tap(MasterScope::lim, sm, left, right, n);
    applyOut(left, right, n, float(p[I::outGain]));
    compTele.push(glue->frames, glue->frameIn, glue->frameGr, glue->frameOut, glue->frameDet);
    clipTele.push(clip->frames, clip->frameIn, clip->frameGr, clip->frameOut, -240);
    limTele.push(lim->frames, lim->frameIn, lim->frameGr, lim->frameOut, -240);
}

void MasterChain::applyUnity(float* l, float* r, int n) {
    const float target = unityTarget;
    float g = unityCur;
    if (g == target && g == 1) return;
    const float k = unityK;
    for (int i = 0; i < n; ++i) {
        g += (target - g) * k;
        l[i] *= g;
        r[i] *= g;
    }
    const float rest = target - g;
    if (rest < 1e-6f && rest > -1e-6f) g = target;
    unityCur = g;
}

void MasterChain::applyOut(float* l, float* r, int n, float target) {
    float g = outCur < 0 ? target : outCur;
    const float k = outK;
    if (g == target) {
        outCur = g;
        if (g == 1) return;
        for (int i = 0; i < n; ++i) {
            l[i] *= g;
            r[i] *= g;
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        g += (target - g) * k;
        l[i] *= g;
        r[i] *= g;
    }
    const float rest = target - g;
    if (rest < 1e-6f && rest > -1e-6f) g = target;
    outCur = g;
}

}  // namespace jambo
