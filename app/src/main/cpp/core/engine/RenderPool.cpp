// Порт Core/Engine/RenderPool.swift (ветка swift JAMbo).
#include "RenderPool.h"
#include <algorithm>

namespace jambo {

RenderPool::RenderPool() {
    for (auto& s : sems) sem_init(&s, 0, 0);
}

RenderPool::~RenderPool() {
    stop();
    for (auto& s : sems) sem_destroy(&s);
}

void RenderPool::start(int k, Setup setup) {
    stop();
    const long g = generation.load(std::memory_order_acquire);
    const int n = std::max(0, std::min(k, maxHelpers));
    if (n == 0) return;
    const int set = int(g & 1) * maxHelpers;
    for (int i = 0; i < n; ++i) {
        sem_t* sem = &sems[set + i];
        threads.emplace_back([this, sem, g, setup] {
            alive.fetch_add(1, std::memory_order_relaxed);
            const std::function<void()> finish = setup();
            helperLoop(sem, g);
            if (finish) finish();
            alive.fetch_sub(1, std::memory_order_relaxed);
        });
    }
    helpers.store(n, std::memory_order_release);
}

void RenderPool::stop() {
    helpers.store(0, std::memory_order_release);
    const long g = generation.fetch_add(1, std::memory_order_acq_rel);
    const int set = int(g & 1) * maxHelpers;
    for (int i = 0; i < maxHelpers; ++i) sem_post(&sems[set + i]);
    for (auto& th : threads)
        if (th.joinable()) th.join();
    threads.clear();
    // Лишние пробуждения неначатых помощников — снять, чтобы новое поколение не проснулось зря.
    for (int i = 0; i < maxHelpers; ++i)
        while (sem_trywait(&sems[set + i]) == 0) {
        }
}

void RenderPool::helperLoop(sem_t* sem, long g) {
    while (generation.load(std::memory_order_acquire) == g) {
        while (sem_wait(sem) != 0) {
        }
        if (generation.load(std::memory_order_acquire) != g) break;
        const int mine = work();
        if (mine > 0) helperJobs.fetch_add(mine, std::memory_order_relaxed);
    }
}

int RenderPool::work() {
    int mine = 0;
    while (true) {
        const int64_t v = ticket.fetch_add(1, std::memory_order_acq_rel);
        const int64_t idx = v & 0xFFFFFFFFLL;
        const int64_t count = v >> 32;
        if (idx >= count) break;
        if (RenderJobs* a = active.load(std::memory_order_acquire)) a->runJob(int(idx));
        mine += 1;
        done.fetch_add(1, std::memory_order_release);
    }
    return mine;
}

void RenderPool::run(int count, RenderJobs& t) {
    if (count <= 0) return;
    allJobs.fetch_add(count, std::memory_order_relaxed);
    const int h = std::min(helpers.load(std::memory_order_acquire), count - 1);
    if (h <= 0) {
        for (int j = 0; j < count; ++j) t.runJob(j);
        return;
    }
    active.store(&t, std::memory_order_release);
    done.store(0, std::memory_order_relaxed);
    ticket.store(int64_t(count) << 32, std::memory_order_release);
    const int set = int(generation.load(std::memory_order_relaxed) & 1) * maxHelpers;
    for (int i = 0; i < h; ++i) sem_post(&sems[set + i]);
    work();
    while (done.load(std::memory_order_acquire) < count) {
    }
}

}  // namespace jambo
