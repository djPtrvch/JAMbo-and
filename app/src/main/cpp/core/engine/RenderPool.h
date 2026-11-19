// Многоядерный режим MULTICORE (порт Core/Engine/RenderPool.swift, ветка swift JAMbo): помощники — потоки, которые ждут на
// семафоре; колбэк будит их, задания блока разбирают все по очереди атомарным счётчиком. Помощник не проснулся — всё
// досчитает колбэк (звук тот же); колбэк ждёт конца взятых заданий. Счётчик: старшие 32 бита — число заданий блока,
// младшие — следующее задание. Приоритет потоков (реальное время) — снаружи (setup).
// Отличие от Swift: семафоры — POSIX (sem_t), потоки — std::thread; stop() дожидается выхода прежних потоков.
#pragma once
#include <semaphore.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace jambo {

/// Задания колбэка, которые можно считать на нескольких потоках (задания независимы).
class RenderJobs {
public:
    virtual ~RenderJobs() = default;
    virtual void runJob(int j) = 0;
};

class RenderPool {
public:
    static constexpr int maxHelpers = 3;
    /// Настройка потока помощника перед работой; возвращает завершение.
    using Setup = std::function<std::function<void()>()>;

    RenderPool();
    ~RenderPool();
    RenderPool(const RenderPool&) = delete;
    RenderPool& operator=(const RenderPool&) = delete;

    int helperCount() const { return helpers.load(std::memory_order_relaxed); }
    int aliveCount() const { return alive.load(std::memory_order_relaxed); }
    /// Заданий, взятых помощниками / всех — с прошлого чтения.
    long takeHelperJobs() { return helperJobs.exchange(0, std::memory_order_relaxed); }
    long takeAllJobs() { return allJobs.exchange(0, std::memory_order_relaxed); }
    /// Запустить k помощников (основной поток; прежние уходят).
    void start(int k, Setup setup = [] { return std::function<void()>([] {}); });
    /// Остановить помощников (основной поток).
    void stop();
    /// Колбэк: задания 0…count−1 на колбэке и помощниках; возврат — когда все сделаны.
    void run(int count, RenderJobs& t);

private:
    std::atomic<int64_t> ticket{0};
    std::atomic<long> done{0};
    std::atomic<long> generation{0};
    std::atomic<int> helpers{0};
    std::atomic<long> helperJobs{0}, allJobs{0};
    std::atomic<int> alive{0};
    /// Два набора семафоров (поколение чётное / нечётное) — старые потоки не крадут пробуждения новых.
    sem_t sems[2 * maxHelpers];
    std::atomic<RenderJobs*> active{nullptr};
    std::vector<std::thread> threads;

    void helperLoop(sem_t* sem, long g);
    int work();
};

}  // namespace jambo
