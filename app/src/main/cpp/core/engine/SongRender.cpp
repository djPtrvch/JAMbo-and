// Офлайн-рендер аранжировки (Audio/SongExport.swift).
#include "SongRender.h"
#include "AudioCore.h"
#include <array>
#include <cmath>

namespace jambo {

bool renderSong(SongRenderSource source, int tail,
                const std::function<bool(const float*, const float*, int)>& sink,
                const std::function<bool()>& cancelled, const std::function<void(double)>& progress) {
    constexpr int rate = int(AudioCore::sampleRate);
    constexpr int block = 256;
    constexpr int chunk = 4096;
    constexpr float quiet = 0.0000316f;
    const int steps = source.project.arrangeEnd();
    const double bpm = source.project.bpm;
    if (steps <= 0 || !std::isfinite(bpm) || bpm < Params::bpmMin || bpm > Params::bpmMax) return false;
    const int64_t songFrames = std::llround(double(steps) * 15 * rate / bpm);
    source.project.arrange.loop = false;
    source.options.refB = false;
    source.options.extra.clear();
    auto engineStorage = std::make_unique<AudioCore>();
    auto& engine = *engineStorage;
    engine.installSynth();
    engine.monitor.paused.store(1);
    engine.publish(source.project, source.samples, source.options);
    engine.events.push(CoreEvent::tempo(bpm));

    std::array<float, block> left{}, right{};
    for (int i = 0; i < 40; ++i) {
        if (cancelled()) return false;
        engine.render(block, left.data(), right.data());
    }
    engine.events.push(CoreEvent::arrange(true, 0));
    engine.events.push(CoreEvent::play(true));
    int64_t written = 0;
    double lastShown = -1.0;
    auto render = [&](int frames) {
        if (cancelled()) return false;
        engine.render(frames, left.data(), right.data());
        // Очереди автономного движка не читает основной поток.
        while (engine.sequencer.out.pop()) {}
        while (engine.arp.out.pop()) {}
        if (!sink(left.data(), right.data(), frames)) return false;
        written += frames;
        const double part = std::min(0.99, double(written) / songFrames);
        if (part - lastShown >= 0.01) {
            lastShown = part;
            progress(part);
        }
        return true;
    };
    while (written < songFrames) {
        if (!render(int(std::min<int64_t>(block, songFrames - written)))) return false;
    }
    engine.events.push(CoreEvent::play(false));
    const int tailFrames = (tail < 0 ? 10 : std::min(tail, 10)) * rate;
    int tailWritten = 0, chunkFrames = 0, quietChunks = 0;
    float peak = 0;
    while (tailWritten < tailFrames) {
        const int n = std::min(block, tailFrames - tailWritten);
        if (!render(n)) return false;
        tailWritten += n;
        if (tail >= 0) continue;
        for (int i = 0; i < n; ++i) peak = std::max({peak, std::abs(left[size_t(i)]), std::abs(right[size_t(i)])});
        chunkFrames += n;
        if (chunkFrames >= chunk) {
            quietChunks = peak < quiet ? quietChunks + 1 : 0;
            if (quietChunks >= 3) break;
            chunkFrames = 0;
            peak = 0;
        }
    }
    return !cancelled();
}

}  // namespace jambo
