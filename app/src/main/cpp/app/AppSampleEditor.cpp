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
    if (n < 1 || operation < 0 || operation > 2) return;
    int begin = 0, end = n;
    if (operation == 0) {
        begin = std::clamp(int(std::round(std::clamp(std::min(a, b), 0.0, 1.0) * n)), 0, n - 1);
        end = std::clamp(int(std::round(std::clamp(std::max(a, b), 0.0, 1.0) * n)), begin + 1, n);
        if (begin == 0 && end == n) return;
    }
    const bool stereo = src->channels() == 2;
    if (operation == 2 && !stereo) return;
    auto out = std::make_shared<SampleData>(end - begin, stereo && operation != 2);
    float gain = 1;
    if (operation == 1) {
        float peak = 0;
        for (int i = 0; i < n; ++i)
            peak = std::max({peak, std::abs(src->left()[i]), std::abs(src->right()[i])});
        if (peak > 0) gain = float(std::pow(10.0, -0.1 / 20)) / peak;
    }
    for (int i = begin; i < end; ++i) {
        out->left()[i - begin] = operation == 2
            ? (src->left()[i] + src->right()[i]) * 0.5f : src->left()[i] * gain;
        if (out->channels() == 2) out->right()[i - begin] = src->right()[i] * gain;
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
        auto marks = t.sliceMarks(key);
        if (operation == 0) {
            const double start = double(begin) / n, finish = double(end) / n;
            marks.erase(std::remove_if(marks.begin(), marks.end(), [&](const SliceMark& m) {
                return m.at < start || m.at >= finish;
            }), marks.end());
            for (auto& m : marks) m.at = (m.at - start) / (finish - start);
        }
        if (const auto warp = t.warps.find(key); warp != t.warps.end()) {
            auto info = warp->second;
            if (operation == 0 && !info.marks.empty()) {
                const double bpm = WarpMap::bpm(&info, src->loopBpm());
                const WarpMap map(info, n, bpm);
                const double shift = begin > 0 ? std::floor(map.beat(begin)) : 0;
                info.bpm = bpm;
                info.marks.erase(std::remove_if(info.marks.begin(), info.marks.end(), [&](const WarpMark& m) {
                    return m.at * n < begin || m.at * n > end;
                }), info.marks.end());
                for (auto& m : info.marks) {
                    m.at = (m.at * n - begin) / (end - begin);
                    m.beat -= shift;
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
    const double gap = std::min(0.5, 1.0 / std::max(1, data->frames()));
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
