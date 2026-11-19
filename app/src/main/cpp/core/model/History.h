// История UNDO / REDO (порт Core/Model/History.swift, ветка swift JAMbo): снимок состояния до действия.
// Место правки (Any? в Swift) — std::any: хранит интерфейс, Core его не знает; пустой — nil.
#pragma once
#include <any>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace jambo {

template <typename T>
class History {
public:
    static constexpr int limit = 200;

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    /// Место записи, которую только что отменили или вернули; пустое — неизвестно.
    const std::any& lastPlace() const { return lastPlace_; }
    /// Растёт при каждом изменении стеков (новая запись, UNDO, REDO).
    int serial() const { return serial_; }

    /// Действие закончено: before — до него, after — после. Внутри жеста — ничего (запись в конце жеста).
    void record(const T& before, const T& after, std::any place = {}) {
        if (gestureDepth > 0 || before == after) return;
        push(before, std::move(place));
    }

    /// Правка вне истории (MORPH) посреди жеста: та же правка — и в начальное состояние жеста.
    void shiftGesture(const std::function<void(T&)>& change) {
        if (gestureDepth <= 0 || !gestureStart) return;
        change(*gestureStart);
    }

    /// Непрерывный жест (поворот ручки, протягивание): одна запись на весь жест.
    void beginGesture(const T& current) {
        if (gestureDepth == 0) gestureStart = current;
        gestureDepth += 1;
    }

    void endGesture(const T& current, std::any place = {}) {
        if (gestureDepth == 0) return;
        gestureDepth -= 1;
        if (gestureDepth == 0 && gestureStart) {
            T start = std::move(*gestureStart);
            gestureStart.reset();
            if (start != current) push(std::move(start), std::move(place));
        }
    }

    /// UNDO: прежнее состояние (current уходит в REDO); nullopt — нечего отменять.
    std::optional<T> undo(const T& current) {
        if (undoStack.empty()) return std::nullopt;
        T prev = std::move(undoStack.back());
        undoStack.pop_back();
        std::any place = std::move(undoPlaces.back());
        undoPlaces.pop_back();
        serial_ += 1;
        redoStack.push_back(current);
        redoPlaces.push_back(place);
        lastPlace_ = std::move(place);
        return prev;
    }

    std::optional<T> redo(const T& current) {
        if (redoStack.empty()) return std::nullopt;
        T next = std::move(redoStack.back());
        redoStack.pop_back();
        std::any place = std::move(redoPlaces.back());
        redoPlaces.pop_back();
        serial_ += 1;
        undoStack.push_back(current);
        undoPlaces.push_back(place);
        lastPlace_ = std::move(place);
        return next;
    }

    /// Новый проект — история сбрасывается.
    void reset() {
        undoStack.clear();
        redoStack.clear();
        undoPlaces.clear();
        redoPlaces.clear();
        lastPlace_.reset();
        gestureDepth = 0;
        gestureStart.reset();
    }

private:
    // deque: removeFirst при переполнении — без сдвига всего стека.
    std::deque<T> undoStack;
    std::vector<T> redoStack;
    std::deque<std::any> undoPlaces;
    std::vector<std::any> redoPlaces;
    std::any lastPlace_;
    int gestureDepth = 0;
    std::optional<T> gestureStart;
    int serial_ = 0;

    void push(T state, std::any place) {
        serial_ += 1;
        redoStack.clear();
        redoPlaces.clear();
        undoStack.push_back(std::move(state));
        undoPlaces.push_back(std::move(place));
        if (int(undoStack.size()) > limit) {
            undoStack.pop_front();
            undoPlaces.pop_front();
        }
    }
};

}  // namespace jambo
