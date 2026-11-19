// Тесты Core: сверка с эталонами. Код возврата 1 — есть непрошедшие случаи.
#include "golden.h"
#include <cstdio>
#include <vector>

std::vector<golden::Result> runFx();
std::vector<golden::Result> runJambo();
int runDrums();
int runTexGran();
int runTables();
int runModel();
int runCoding();
int runEdits();
int runAudio();

int main() {
    int fails = 0, total = 0;
    std::printf("%-28s %10s %10s %10s  %s\n", "case", "sample dB", "band dB", "rms dBFS", "result");
    std::vector<golden::Result> all = runFx();
    for (auto& r : runJambo()) all.push_back(r);
    for (const auto& r : all) {
        ++total;
        const bool ok = r.ok();
        if (!ok) ++fails;
        std::printf("%-28s %10.1f %10.2f %10.1f  %s\n", r.file.c_str(), r.sampleErrorDb, r.maxBandDiffDb, r.rmsDb,
                    ok ? "OK" : "FAIL");
    }
    std::printf("golden: %d / %d passed\n\n", total - fails, total);
    const int drumFails = runDrums() + runTexGran() + runTables() + runModel() + runCoding() + runEdits() + runAudio();
    std::printf("properties: %s\n", drumFails == 0 ? "all passed" : "FAILED");
    return fails == 0 && total > 0 && drumFails == 0 ? 0 : 1;
}
