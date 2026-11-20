// Запись аранжировки из сетки (порт Core/Model/ArrangeRecorder.swift, ветка swift JAMbo): что играет и слышно в сетке,
// пишется отрезками в позициях курсора; на конце записи отрезки ложатся в проект одной правкой
// (Project::applyRecording, ArrangeRecorder.cpp). Дорожки, которые идут по таймлайну, не пишутся.
#pragma once
#include <map>
#include <optional>
#include <set>
#include <vector>
#include "Clip.h"
#include "Params.h"

namespace jambo {

class ArrRecorder {
public:
    /// Отрезок: дорожка, клип (−1 — тишина), начало и конец (шаги таймлайна), место в проходе клипа на начале;
    /// silent — клип играл заглушённым (mute / solo): на таймлайне пусто.
    struct Seg {
        int track = 0, slot = 0, start = 0, end = 0, offset = 0;
        bool silent = false;
        Seg() = default;
        Seg(int t, int s, int a, int e, int o, bool q = false) : track(t), slot(s), start(a), end(e), offset(o), silent(q) {}
        auto tie() const { return std::tie(track, slot, start, end, offset, silent); }
        JAMBO_EQ(Seg)
    };
    /// Живая нота: дорожка, шаг таймлайна, ноты, сила (0…1; nullopt — без своей), длина в шагах.
    struct LiveNote {
        int track = 0, at = 0;
        std::vector<int> notes;
        std::optional<double> velocity;
        int len = 1;
        auto tie() const { return std::tie(track, at, notes, velocity, len); }
        JAMBO_EQ(LiveNote)
    };
    /// Клип и место в проходе на начале записи; nullopt — дорожка на таймлайне, не пишется.
    using State = std::optional<std::pair<int, int>>;

    explicit ArrRecorder(int tracks = Params::maxTracks);

    const std::vector<Seg>& done() const { return done_; }
    const std::vector<std::map<int, int>>& played() const { return played_; }
    const std::vector<std::set<int>>& fired() const { return fired_; }
    const std::vector<std::map<int, Step>>& heard() const { return heard_; }
    const std::vector<LiveNote>& live() const { return live_; }
    bool active() const { return active_; }
    int pos() const { return pos_; }

    /// Отрезки записи сейчас (законченные и идущие — до конца текущего шага).
    std::vector<Seg> current() const;
    /// Клип, который сейчас пишется у дорожки t (−1 — тишина; nullopt — дорожка не пишется).
    std::optional<int> openSlot(int t) const;
    /// Шаг p клипа дорожки t начал звучать на текущем шаге таймлайна; step — каким он был.
    void notePlayed(int t, int p, const Step* step = nullptr);
    /// Нажали клавишу: ноты на шаге at; номер ноты (для длины) или nullopt — дорожка не пишется.
    std::optional<int> liveNote(int t, int at, const std::vector<int>& notes, std::optional<double> velocity);
    void liveLength(int i, int len);
    /// Шаг дорожки t прошёл Chance на текущем шаге таймлайна.
    void noteFired(int t);
    void begin(int at, const std::vector<State>& states, const std::vector<bool>& audible = {});
    /// Mute / solo: сменилась слышимость — отрезок закрывается на шаге at и продолжается тем же клипом.
    void audible(const std::vector<bool>& flags, int at);
    /// Курсор на шаге p: скачок (петля) — отрезки закрываются и продолжаются с p.
    void step(int p);
    /// Дорожка t с текущего шага играет клип slot (−1 — тишина; nullopt — ушла на таймлайн).
    void clip(int t, std::optional<int> slot);
    /// Дорожка from встала на место to.
    void move(int from, int to);
    /// Конец записи — на конце текущего шага; все отрезки по порядку записи.
    std::vector<Seg> finish();

private:
    std::vector<std::optional<Seg>> open_;
    std::vector<bool> muted_;
    std::vector<Seg> done_;
    std::vector<std::map<int, int>> played_;
    std::vector<std::set<int>> fired_;
    std::vector<std::map<int, Step>> heard_;
    std::vector<LiveNote> live_;
    bool active_ = false;
    int pos_ = 0;
    void close(int t, int end);
};

}  // namespace jambo
