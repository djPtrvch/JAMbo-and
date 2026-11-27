#include "App.h"
#include "engine/Capture.h"
#include "model/SynthEngines.h"
#include <ctime>

namespace jambo {

std::string App::editorKey() const {
    if (channel_ != Channel::track || SynthKey::engine(track().sample) >= 0) return {};
    return track().sample;
}

std::string App::editorName() const {
    const auto key = libSaveMode() && !browser_->sample.empty() ? browser_->sample : editorKey();
    return key.empty() ? "" : sampleName(key);
}

void App::editorEdit(int operation, double a, double b) {
    const auto key = editorKey();
    const auto src = sampleData(key);
    if (!src || !samples_ || !std::isfinite(a) || !std::isfinite(b)) return;
    const int n = src->frames();
    if (n < 1 || operation < 0 || operation > 4) return;
    if (operation == 4) {
        stopLibraryPreview();
        editTrack([](Track& t) { t.clearSample(); });
        return;
    }
    const bool cut = operation == 3;
    int begin = 0, end = n;
    if (operation == 0 || cut) {
        begin = std::clamp(int(std::round(std::clamp(std::min(a, b), 0.0, 1.0) * n)), 0, n - 1);
        end = std::clamp(int(std::round(std::clamp(std::max(a, b), 0.0, 1.0) * n)), begin + (cut ? 0 : 1), n);
        if ((begin == 0 && end == n) || (cut && begin == end)) return;
    }
    const bool stereo = src->channels() == 2;
    if (operation == 2 && !stereo) return;
    const int count = cut ? n - (end - begin) : end - begin;
    auto out = std::make_shared<SampleData>(count, stereo && operation != 2);
    float gain = 1;
    if (operation == 1) {
        float peak = 0;
        for (int i = 0; i < n; ++i)
            peak = std::max({peak, std::abs(src->left()[i]), std::abs(src->right()[i])});
        if (peak > 0) gain = float(std::pow(10.0, -0.1 / 20)) / peak;
    }
    if (cut) {
        const int fade = begin > 0 ? std::min(240, n - end) : 0;
        const auto copy = [&](const float* in, float* dst) {
            std::copy(in, in + begin, dst);
            std::copy(in + end, in + n, dst + begin);
            for (int i = 0; i < fade; ++i) {
                const float w = float(i + 1) / float(fade + 1);
                dst[begin + i] = in[begin + i] * (1 - w) + in[end + i] * w;
            }
        };
        copy(src->left(), out->left());
        if (stereo) copy(src->right(), out->right());
    } else {
        for (int i = begin; i < end; ++i) {
            out->left()[i - begin] = operation == 2
                ? (src->left()[i] + src->right()[i]) * 0.5f : src->left()[i] * gain;
            if (out->channels() == 2) out->right()[i - begin] = src->right()[i] * gain;
        }
    }
    const auto next = SampleStore::store(*out, samples_->dir());
    if (next.empty() || next == key) return;
    stopLibraryPreview();
    samples_->add(next, out);
    const int selected = selected_;
    edit([&](Project& p) {
        auto& t = p.tracks[size_t(selected)];
        p.files[next] = sampleName(key);
        if (const auto root = p.roots.find(key); root != p.roots.end()) p.roots[next] = root->second;
        const auto remap = [&](double at, bool warpMark) -> std::optional<double> {
            const double pos = at * n;
            if (cut) {
                if (pos <= begin) return pos / count;
                if (pos < end) return std::nullopt;
                return (pos - (end - begin)) / count;
            }
            if (pos < begin || pos > end || (!warpMark && pos == end)) return std::nullopt;
            return (pos - begin) / count;
        };
        const double gap = WarpMap::minFrames / count;
        std::vector<SliceMark> marks;
        for (auto m : t.sliceMarks(key)) {
            if (const auto at = remap(m.at, false)) {
                const auto last = std::find_if(marks.rbegin(), marks.rend(), [&](const auto& x) {
                    return x.isFree() == m.isFree();
                });
                if (cut && last != marks.rend() && *at - last->at < gap) continue;
                m.at = *at;
                marks.push_back(m);
            }
        }
        if (const auto old = t.warps.find(key); old != t.warps.end() && !old->second.isEmpty()) {
            const auto& warp = old->second;
            WarpInfo info{warp.bpm, {}, warp.off};
            if (!warp.marks.empty()) {
                const double bpm = WarpMap::bpm(&warp, src->loopBpm(),
                    warpFollow_ ? std::optional<double>(p.bpm) : std::nullopt);
                const WarpMap map(warp, n, bpm);
                const double offset = cut ? map.beat(end) - map.beat(begin)
                    : begin > 0 ? std::floor(map.beat(begin)) : 0;
                info.bpm = bpm;
                for (auto m : warp.marks) {
                    if (const auto at = remap(m.at, true)) {
                        if (!cut || m.at * n >= end) m.beat -= offset;
                        if (cut && !info.marks.empty() && (*at - info.marks.back().at < gap ||
                            m.beat - info.marks.back().beat < WarpMap::minBeat)) continue;
                        m.at = *at;
                        info.marks.push_back(m);
                    }
                }
            }
            t.warps[next] = std::move(info);
        }
        if (marks.empty()) t.slices.erase(next);
        else t.slices[next] = std::move(marks);
        t.replaceSample(key, next);
    });
}

void App::editorSlice(int index, double at) {
    const auto key = editorKey();
    const auto data = sampleData(key);
    if (!data || !std::isfinite(at)) return;
    auto marks = track().sliceMarks(key);
    const double gap = (index < 0 ? 1.0 : 32.0) / std::max(1, data->frames());
    if (index < 0) {
        if (at < 0 || at >= 1 || marks.size() >= 128) return;
        auto pos = std::lower_bound(marks.begin(), marks.end(), at,
            [](const SliceMark& mark, double x) { return mark.at < x; });
        if ((pos != marks.end() && pos->at - at < gap) ||
            (pos != marks.begin() && at - std::prev(pos)->at < gap)) return;
        marks.emplace(pos, at);
    } else {
        if (size_t(index) >= marks.size()) return;
        if (at < 0) marks.erase(marks.begin() + index);
        else {
            const double lo = index == 0 ? 0 : marks[size_t(index - 1)].at + gap;
            const double hi = size_t(index + 1) == marks.size() ? 1 - gap : marks[size_t(index + 1)].at - gap;
            if (lo > hi) return;
            marks[size_t(index)].at = std::clamp(at, lo, hi);
        }
    }
    edit([&](Project& p) {
        auto& all = p.tracks[size_t(selected_)].slices;
        if (marks.empty()) all.erase(key);
        else all[key] = marks;
    });
}

std::vector<std::string> App::editorSliceLabels() const {
    const auto marks = track().sliceMarks(editorKey());
    const auto notes = Slices::notes(marks, sliceMap_, project_.scale, project_.root);
    const std::string roles[] = {"KICK", "SNARE", "HAT", "PAD", "SNARE", "PERC", "BASS", "LEAD"};
    std::vector<std::string> labels;
    for (size_t i = 0; i < marks.size(); ++i) {
        const int role = marks[i].role.value_or(-1);
        labels.push_back(!marks[i].isFree() ? Params::noteName(notes[i])
            : role >= 0 && role < 8 ? roles[role] : "NO NOTE");
    }
    return labels;
}

std::vector<int> App::editorSliceRoles() const {
    std::vector<int> roles;
    for (const auto& m : track().sliceMarks(editorKey())) {
        const int r = m.role.value_or(-1);
        roles.push_back(r == 4 ? 1 : r);
    }
    return roles;
}

bool App::editorAssignSlice(int index, int note) {
    const auto key = editorKey();
    auto marks = track().sliceMarks(key);
    if (index < 0 || index >= int(marks.size()) || note > 127 || note < -1) return false;
    if (note >= 0) {
        const auto notes = Slices::notes(marks, sliceMap_, project_.scale, project_.root);
        for (int i = 0; i < int(notes.size()); ++i) if (i != index && notes[size_t(i)] == note) return false;
        marks[size_t(index)].free.reset();
        marks[size_t(index)].note = note;
    } else {
        marks[size_t(index)].note.reset();
        marks[size_t(index)].free = true;
    }
    editTrack([&](Track& t) { t.slices[key] = marks; });
    return true;
}

void App::editorSliceRole(int index, int role) {
    const auto key = editorKey();
    auto marks = track().sliceMarks(key);
    if (index < 0 || index >= int(marks.size()) || role < 0 || role > 7 || role == 4) return;
    auto& selected = marks[size_t(index)].role;
    const int current = selected.value_or(-1);
    if (current == role || (current == 4 && role == 1)) selected.reset();
    else selected = role;
    editTrack([&](Track& t) { t.slices[key] = marks; });
}

void App::editorPreview(double a, double b) {
    if (previewOn_) {
        stopLibraryPreview();
        return;
    }
    const auto key = editorKey();
    if (!sampleData(key) || !std::isfinite(a) || !std::isfinite(b) || a >= b) return;
    libPreviewKey_ = key;
    publishAudio();
    previewOn_ = true;
    audio_->push(CoreEvent::preview(SoundKey::hash(key), a, b));
}

void App::editorSave() {
    const auto key = editorKey();
    if (!sampleData(key)) return;
    Json marks{{"slices", json::put(track().sliceMarks(key))}};
    if (const auto warp = track().warps.find(key); warp != track().warps.end())
        marks["warp"] = json::put(warp->second);
    openBrowser({});
    browser_->sample = key;
    browser_->sampleMarks = std::move(marks);
}

void App::applyLibraryMarks(Track& t, const std::string& key) const {
    const auto found = library_.marks.find(key);
    if (found == library_.marks.end() || t.slices.count(key) || t.warps.count(key)) return;
    auto slices = json::value(found->second, "slices", std::vector<SliceMark>{});
    if (!slices.empty()) t.slices[key] = std::move(slices);
    if (auto warp = json::opt<WarpInfo>(found->second, "warp")) t.warps[key] = std::move(*warp);
}

bool App::startSampleCapture(int source) {
    if (sampleCapturing() || channel_ != Channel::track || !samples_ ||
        source < -1 || (source != StemRing::master && source >= int(project_.tracks.size()))) return false;
    if (source != -1) {
        auto* ring = audio_->stemRing();
        if (!ring || stems_.recording().count(StemRing::capture)) return false;
        ring->prepare();
        audio_->push(CoreEvent::captureStart(source));
    }
    capturePeaks_ = RecPeaks{};
    captureTrack_ = track().id;
    captureProject_ = session_.id;
    captureProjectName_ = session_.name;
    captureName_ = source == -1 ? "MIC" : source == StemRing::master ? "MASTER" : project_.tracks[size_t(source)].name;
    captureDeadline_ = source == -1 ? 0 : now_ + Capture::maxSeconds + 2;
    bump();
    return true;
}

std::vector<float> App::capturePeaks(int columns) {
    if (!sampleCapturing() || columns < 1 || columns > 2048) return {};
    capturePeaks_.update(stems_.bufferL(StemRing::capture), stems_.bufferR(StemRing::capture));
    std::vector<float> out(size_t(columns), 0);
    const int span = capturePeaks_.span();
    for (int col = 0; col < columns; ++col) {
        const int begin = col * span / columns;
        const int end = (col + 1) * span / columns;
        const auto l = capturePeaks_.range(0, begin, end);
        const auto r = capturePeaks_.range(1, begin, end);
        out[size_t(col)] = std::max({-l.first, l.second, -r.first, r.second});
    }
    return out;
}

void App::stopSampleCapture() {
    if (!sampleCapturing()) return;
    audio_->push(CoreEvent::make(CoreEvent::Kind::captureStop));
    captureDeadline_ = now_ + 2;
}

void App::finishSampleCapture(const SampleRef& data) {
    if (!captureTrack_) return;
    const int id = *captureTrack_;
    captureTrack_.reset();
    bump();
    if (!data || !samples_) return;
    const std::string key = SampleStore::store(*data, samples_->dir());
    if (key.empty()) return;
    samples_->add(key, data);
    const std::time_t now = std::time(nullptr);
    std::tm date{};
    localtime_r(&now, &date);
    const auto name = Library::recordingName(captureName_, date.tm_hour, date.tm_min, date.tm_sec);
    if (captureProject_ == session_.id) edit([&](Project& p) {
        const auto t = std::find_if(p.tracks.begin(), p.tracks.end(), [id](const Track& item) { return item.id == id; });
        if (t == p.tracks.end()) return;
        p.files[key] = name;
        t->addRecordedSample(key);
    });
    const auto folder = Library::recordingFolderName(captureProjectName_, date.tm_mday, date.tm_mon + 1, date.tm_year + 1900);
    libEdit([&](Library& lib) { lib.addSample(lib.recordingsFolder(folder), key, name); });
}

}  // namespace jambo
