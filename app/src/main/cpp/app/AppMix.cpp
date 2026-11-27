// Микшер (порт части AppModelSound.swift — mute, solo, меню дорожки, перестановка — и AppModelStems.swift, ветка swift
// JAMbo): кнопки M, S, ● у ручек громкости в MIX, ● MASTER; стем — сэмпл в RECORDINGS библиотеки, в папке
// «проект дд.мм.гггг», с именем «дорожка чч-мм-сс». Правила начала и конца записи — в колбэке (SequencerStems).
#include "App.h"
#include <ctime>

namespace jambo {

// MARK: — mute, solo (без UNDO, решение пользователя)

void App::toggleMute(int i) {
    if (i < 0 || i >= int(project_.tracks.size())) return;
    mutatePlay([&](Project& p) {
        auto& t = p.tracks[size_t(i)];
        t.mute = !t.mute;
        if (t.mute) t.solo = false;
    }, true);
    bump();
}

void App::toggleSolo(int i) {
    if (i < 0 || i >= int(project_.tracks.size())) return;
    mutatePlay([&](Project& p) {
        auto& t = p.tracks[size_t(i)];
        t.solo = !t.solo;
        if (t.solo) t.mute = false;
    }, true);
    bump();
}

// MARK: — меню дорожки

void App::openTrackMenu(int slot) {
    if (slot < int(project_.tracks.size())) trackMenu_ = TrackMenu{slot, false};
    else if (project_.canAddTrack()) trackMenu_ = TrackMenu{slot, true};
    bump();
}

void App::closeTrackMenu() {
    trackMenu_.reset();
    bump();
}

void App::addTrack() {
    trackMenu_.reset();
    const KnobDefaults kd = knobDefaults_;
    edit([&](Project& p) {
        p.addTrack();
        if (!p.tracks.empty()) kd.apply(p.tracks.back());
    });
    // Новая дорожка сразу играет свой клип (решение пользователя): при игре — с ближайшей границы, как тап по
    // ячейке; без игры — запустится со стартом. Строка — играющая, иначе та, что играет у большинства дорожек.
    const int i = int(project_.tracks.size()) - 1;
    int row = 0;
    if (playing_ && sceneRow_ >= 0) {
        row = sceneRow_;
    } else {
        std::map<int, int> count;
        for (const auto& t : project_.tracks)
            if (!t.stopped && !t.blank()) count[t.slot] += 1;
        int best = -1, votes = 0;
        for (const auto& [k, c] : count)
            if (c > votes) {
                best = k;
                votes = c;
            }
        if (best >= 0) row = best;
    }
    if (i >= 0 && project_.tracks[size_t(i)].clipFilled(row)) queueClip(i, row);
}

void App::copyTrack(int i) {
    trackMenu_.reset();
    if (i < 0 || i >= int(project_.tracks.size())) return;
    trackClipboard_ = project_.tracks[size_t(i)];
    bump();
}

void App::pasteTrack(int i) {
    trackMenu_.reset();
    if (!trackClipboard_ || i < 0 || i >= int(project_.tracks.size())) return;
    const Track c = *trackClipboard_;
    edit([&](Project& p) { p.pasteTrack(i, c); });
    if (i == selected_) {
        held_.clear();
        viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
    }
}

void App::deleteTrack(int i) {
    trackMenu_.reset();
    if (!project_.canDeleteTrack(i)) return;
    const int sel = selected_;
    const int n = int(project_.tracks.size()) - 1;
    const int next = std::max(0, std::min(n - 1, sel > i ? sel - 1 : sel));
    if (next != sel) selected_ = next;
    edit([&](Project& p) { p.deleteTrack(i); });
    mixAn_.reset();
    selected_ = std::min(selected_, int(project_.tracks.size()) - 1);
    bump();
}

void App::renameTrack(int i, const std::string& name) {
    if (i < 0 || i >= int(project_.tracks.size())) return;
    edit([&](Project& p) { p.renameTrack(i, name); });
}

/// Перестановка в MIX: выбранная дорожка остаётся выбранной; во время игры ход дорожек переезжает с ними.
void App::moveTrack(int from, int to) {
    const int n = int(project_.tracks.size());
    if (from == to || from < 0 || to < 0 || from >= n || to >= n) return;
    const int id = track().id;
    finishArrangeAuto(true);
    if (arrangeRecorder_.active()) arrangeRecorder_.move(from, to);
    edit([&](Project& p) { p.moveTrack(from, to); });
    mixAn_.reset();
    if (playing_) {
        audio_->push(CoreEvent::moveTrack(from, to));
        auto mv = [&](auto& a) {
            auto x = a[size_t(from)];
            if (from < to) for (int k = from; k < to; k++) a[size_t(k)] = a[size_t(k + 1)];
            else for (int k = from; k > to; k--) a[size_t(k)] = a[size_t(k - 1)];
            a[size_t(to)] = x;
        };
        mv(queued_);
        mv(trackStart_);
        mv(playPos_);
        mv(cell_);
    }
    for (int k = 0; k < int(project_.tracks.size()); k++)
        if (project_.tracks[size_t(k)].id == id) selected_ = k;
    bump();
}

// MARK: — запись стемов и мастера

void App::stemTap(int ch) {
    StemRing* ring = audio_->stemRing();
    if (!ring) return;
    if (stemRecording_.count(ch)) {
        stemArmed_.erase(ch);
        audio_->push(CoreEvent::stemFinish(ch));
    } else if (stemArmed_.count(ch)) {
        stemArmed_.erase(ch);
        audio_->push(CoreEvent::stemArm(ch, false));
    } else {
        // Кольцо записи — при первом нажатии (не при запуске).
        ring->prepare();
        stemArmed_.insert(ch);
        audio_->push(CoreEvent::stemArm(ch, true));
    }
    bump();
}

std::pair<bool, bool> App::stemDot(int ch) const {
    if (!stemRecording_.count(ch)) return {false, false};
    const auto it = stems_.endAt().find(ch);
    if (it == stems_.endAt().end()) return {true, false};
    const int64_t left = it->second - int64_t(audio_->framesRendered());
    return {true, left <= int64_t(5 * 48000)};
}

void App::drainStems() {
    StemRing* ring = audio_->stemRing();
    if (!ring) return;
    const auto done = stems_.poll(*ring);
    std::set<int> now = stems_.recording();
    now.erase(StemRing::capture);
    if (now != stemRecording_) {
        // Имя дорожки — на момент начала записи.
        for (int c : now)
            if (!stemRecording_.count(c))
                stemNames_[c] = c == StemRing::master ? std::string("MASTER")
                                : c < int(project_.tracks.size()) ? project_.tracks[size_t(c)].name : std::to_string(c + 1);
        stemRecording_ = now;
        bump();
    }
    for (const auto& s : done) {
        if (s.channel == StemRing::capture) {
            finishSampleCapture(s.data);
            continue;
        }
        if (!s.data) continue;
        std::string base = stemNames_.count(s.channel) ? stemNames_[s.channel] : std::to_string(s.channel + 1);
        stemNames_.erase(s.channel);
        saveStem(s.data, base);
    }
}

void App::saveStem(const SampleRef& data, const std::string& base) {
    if (!samples_) return;
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_r(&t, &lt);
    const std::string name = Library::recordingName(base, lt.tm_hour, lt.tm_min, lt.tm_sec);
    const std::string folder = Library::recordingFolderName(session_.name, lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
    const std::string key = SampleStore::store(*data, samples_->dir());
    if (key.empty()) return;
    samples_->add(key, data);
    libEdit([&](Library& lib) {
        const std::string id = lib.recordingsFolder(folder);
        lib.addSample(id, key, name);
    });
}

}  // namespace jambo
