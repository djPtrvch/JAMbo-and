// Порт MasterChain.fill (Core/Master/MasterChain.swift, ветка swift JAMbo).
#include "MasterChain.h"
#include "model/Project.h"

namespace jambo {
namespace MasterChain {

/// RELEASE при AUTO.
static constexpr double autoRel = 400;

void fill(double* p, const Project& project) {
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

}  // namespace MasterChain
}  // namespace jambo
