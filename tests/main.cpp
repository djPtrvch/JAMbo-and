// Тесты Core: сверка с эталонами. Код возврата 1 — есть непрошедшие случаи.
#include "golden.h"
#include <cstdio>
#include <vector>

std::vector<golden::Result> runFx();

int main() {
    int fails = 0, total = 0;
    std::printf("%-28s %10s %10s %10s  %s\n", "case", "sample dB", "band dB", "rms dBFS", "result");
    for (const auto& r : runFx()) {
        ++total;
        const bool ok = r.ok();
        if (!ok) ++fails;
        std::printf("%-28s %10.1f %10.2f %10.1f  %s\n", r.file.c_str(), r.sampleErrorDb, r.maxBandDiffDb, r.rmsDb,
                    ok ? "OK" : "FAIL");
    }
    std::printf("%d / %d passed\n", total - fails, total);
    return fails == 0 && total > 0 ? 0 : 1;
}
