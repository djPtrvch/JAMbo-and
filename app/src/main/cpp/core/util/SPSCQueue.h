// Очередь «один писатель — один читатель» без блокировок и выделений после создания (порт Core/Util/SPSCQueue.swift,
// ветка swift JAMbo). T — простой тип: элемент копируется в заранее выделенный буфер.
#pragma once
#include <algorithm>
#include <atomic>
#include <optional>
#include <vector>

namespace jambo {

template <typename T>
class SPSCQueue {
public:
    /// capacity округляется вверх до степени двойки; filler — начальное значение ячеек.
    SPSCQueue(int capacity, const T& filler) {
        int size = 1;
        while (size < std::max(2, capacity)) size <<= 1;
        capacity_ = size;
        mask = size - 1;
        buffer.assign(size_t(size), filler);
    }

    /// Положить элемент; false — очередь полна. Не ждёт — годится и для аудиопотока.
    bool push(const T& value) {
        const long h = head.load(std::memory_order_relaxed);
        const long t = tail.load(std::memory_order_acquire);
        if (h - t >= capacity_) return false;
        buffer[size_t(h & mask)] = value;
        head.store(h + 1, std::memory_order_release);
        return true;
    }

    /// Взять элемент; nullopt — пусто.
    std::optional<T> pop() {
        const long t = tail.load(std::memory_order_relaxed);
        const long h = head.load(std::memory_order_acquire);
        if (t == h) return std::nullopt;
        T value = buffer[size_t(t & mask)];
        tail.store(t + 1, std::memory_order_release);
        return value;
    }

    /// Сколько элементов ждёт (приблизительно, если другой поток работает).
    int count() const {
        return int(head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire));
    }

private:
    std::vector<T> buffer;
    int capacity_ = 0;
    long mask = 0;
    std::atomic<long> head{0};
    std::atomic<long> tail{0};
};

}  // namespace jambo
