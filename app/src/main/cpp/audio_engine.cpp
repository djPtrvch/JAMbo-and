#include "audio_engine.h"
#include "link_session.h"
#include <oboe/OboeExtensions.h>
#include <cmath>
#include <ctime>
#include <android/log.h>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "JAMbo", __VA_ARGS__)

namespace {
constexpr float kTwoPi = 6.28318530718f;
int64_t monoNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}
double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}

void AudioEngine::ensureCore() {
    std::lock_guard<std::mutex> g(produce_);
    if (core_) return;
    core_ = std::make_unique<jambo::AudioCore>();
    core_->installSynth();
    coreL_.assign(jambo::AudioCore::maxFrames, 0.f);
    coreR_.assign(jambo::AudioCore::maxFrames, 0.f);
}

void AudioEngine::push(const jambo::CoreEvent& e) {
    ensureCore();
    std::lock_guard<std::mutex> g(produce_);
    core_->events.push(e);
    core_->releaseRetired(nowSec());
}

bool AudioEngine::start() {
    ensureCore();
    std::lock_guard<std::mutex> g(lock_);
    wanted_ = true;
    return openLocked();
}

bool AudioEngine::openLocked() {
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setSampleRate(48000)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setUsage(oboe::Usage::Media)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    oboe::Result r = b.openStream(stream_);
    if (r != oboe::Result::OK) { LOGI("openStream: %s", oboe::convertToText(r)); return false; }
    // Два «бёрста» — рекомендованный минимум без треска.
    stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);
    // ADPF: система видит время колбэка и поднимает частоту или переносит его на большое ядро (если устройство умеет).
    stream_->setPerformanceHintEnabled(true);
    tuner_ = std::make_unique<oboe::LatencyTuner>(*stream_);
    sr_ = stream_->getSampleRate();
    frameCount_ = 0;
    tapFrame_.store(-1);
    r = stream_->requestStart();
    if (r != oboe::Result::OK) { LOGI("start: %s", oboe::convertToText(r)); return false; }
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> g(lock_);
    wanted_ = false;
    tuner_.reset();
    if (stream_) { stream_->stop(); stream_->close(); stream_.reset(); }
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result r) {
    // Отключили наушники / сменился выход — открываем поток заново.
    LOGI("stream closed: %s, reopening", oboe::convertToText(r));
    std::lock_guard<std::mutex> g(lock_);
    tuner_.reset();
    stream_.reset();
    if (wanted_) openLocked();
}

void AudioEngine::tap(int64_t touchNanos) {
    tapTouchNs_.store(touchNanos);
    tapSeq_.fetch_add(1);
}

double AudioEngine::modOut(int i) const {
    if (!core_ || i < 0 || i >= jambo::ModEngine::tracks * 4) return 0;
    return core_->mod.outs[size_t(i)].load();
}

jambo::PlayClock::State AudioEngine::clock() const {
    if (!core_) return {};
    return core_->sequencer.clock.read();
}

double AudioEngine::heardNow() const { return nowSec() - double(outLatencyUs_.load()) / 1e6; }

double AudioEngine::modPhase(int i) const {
    if (!core_ || i < 0 || i >= jambo::ModEngine::tracks * 3) return 0;
    return core_->mod.phaseOuts[size_t(i)].load();
}

double AudioEngine::envOut(int i) const {
    if (!core_ || i < 0 || i >= jambo::ModEngine::tracks * 2) return i % 2 == 0 ? -1 : 0;
    return core_->mod.envOuts[size_t(i)].load();
}

void AudioEngine::publish(const jambo::Project& p, const jambo::SoundSnapshot::Options& o,
                          const std::map<std::string, jambo::SampleRef>& samples) {
    ensureCore();
    std::lock_guard<std::mutex> g(produce_);
    core_->publish(p, samples, o, nowSec());
    core_->releaseRetired(nowSec());
}

bool AudioEngine::popOut(jambo::SeqOut& e) {
    if (!core_) return false;
    auto x = core_->sequencer.out.pop();
    if (!x) return false;
    e = *x;
    return true;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream*, void* data, int32_t frames) {
    const int64_t t0 = monoNs();
    if (tuner_) tuner_->tune();
    float* out = static_cast<float*>(data);
    const float sr = float(sr_);

    // TAP: щелчок начинается с первого отсчёта этого буфера.
    uint32_t seq = tapSeq_.load(std::memory_order_acquire);
    if (seq != tapSeen_) {
        tapSeen_ = seq;
        clickEnv_ = 1.0f; clickPhase_ = 0; clickFreq_ = 1500;
        tapTouchSeen_.store(tapTouchNs_.load());
        tapFrame_.store(frameCount_);
    }

    const bool toneOn = tone_.load(std::memory_order_relaxed);
    const float toneStep = kTwoPi * 440.0f / sr;
    const float rampK = 1.0f / (0.005f * sr);            // 5 мс
    const float clickDecay = std::exp(-1.0f / (0.004f * sr));

    for (int i = 0; i < frames; ++i) {
        float s = 0;
        float target = toneOn ? 0.2f : 0.0f;
        if (toneGain_ < target) toneGain_ = std::fmin(target, toneGain_ + rampK * 0.2f);
        else if (toneGain_ > target) toneGain_ = std::fmax(target, toneGain_ - rampK * 0.2f);
        if (toneGain_ > 0) {
            s += toneGain_ * std::sin(tonePhase_);
            tonePhase_ += toneStep;
            if (tonePhase_ > kTwoPi) tonePhase_ -= kTwoPi;
        }
        if (clickEnv_ > 1e-4f) {
            s += 0.5f * clickEnv_ * std::sin(clickPhase_);
            clickPhase_ += kTwoPi * clickFreq_ / sr;
            clickEnv_ *= clickDecay;
        }
        out[2 * i] = s;
        out[2 * i + 1] = s;
    }

    // Ядро: блоками не длиннее AudioCore::maxFrames, планарно → чередование каналов.
    if (core_) {
        float* cl = coreL_.data();
        float* cr = coreR_.data();
        // Время хоста блока (часы колбэка): по нему — запись с REC (heardNow).
        const double host = nowSec();
        for (int at = 0; at < frames;) {
            const int n = std::min(int(frames) - at, jambo::AudioCore::maxFrames);
            core_->render(n, cl, cr, host + double(at) / sr_);
            for (int i = 0; i < n; ++i) {
                out[2 * (at + i)] += cl[i];
                out[2 * (at + i) + 1] += cr[i];
            }
            at += n;
        }
    }

    frameCount_ += frames;
    lastFrames_.store(frames, std::memory_order_relaxed);

    // Загрузка колбэка: время расчёта / длительность буфера.
    const float load = float(double(monoNs() - t0) / (1e9 * frames / sr_));
    const float avg = loadAvg_.load(std::memory_order_relaxed);
    loadAvg_.store(avg + 0.02f * (load - avg), std::memory_order_relaxed);
    if (load > loadMaxHold_) loadMaxHold_ = load;
    if (++loadMaxTicks_ >= 400) {  // ~1–2 с
        loadMax_.store(loadMaxHold_, std::memory_order_relaxed);
        loadMaxHold_ = 0; loadMaxTicks_ = 0;
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::updateLatency() {
    std::lock_guard<std::mutex> g(lock_);
    if (!stream_) return;
    auto lat = stream_->calculateLatencyMillis();
    if (lat) outLatencyUs_.store(int64_t(lat.value() * 1000.0));
}

void AudioEngine::info(double* o) {
    for (int i = 0; i < kInfoSize; ++i) o[i] = -1;
    std::lock_guard<std::mutex> g(lock_);
    if (!stream_) { o[0] = 0; return; }
    auto* s = stream_.get();
    o[0] = 1;                                         // поток открыт
    o[1] = s->getSampleRate();
    o[2] = s->getFramesPerBurst();
    o[3] = s->getBufferSizeInFrames();
    o[4] = s->getBufferCapacityInFrames();
    o[5] = lastFrames_.load();
    o[6] = s->getAudioApi() == oboe::AudioApi::AAudio ? 2 : 1;   // 2 AAudio, 1 OpenSL ES
    o[7] = s->getSharingMode() == oboe::SharingMode::Exclusive ? 1 : 0;
    o[8] = s->getPerformanceMode() == oboe::PerformanceMode::LowLatency ? 1 : 0;
    o[9] = oboe::OboeExtensions::isMMapUsed(s) ? 1 : 0;
    auto lat = s->calculateLatencyMillis();
    o[10] = lat ? lat.value() : -1;
    o[18] = lat ? 0 : double(int(lat.error()));      // код ошибки метки времени
    o[19] = double(int(s->getState()));
    if (lat) outLatencyUs_.store(int64_t(lat.value() * 1000.0));
    o[11] = loadAvg_.load() * 100.0;
    o[12] = loadMax_.load() * 100.0;
    auto xr = s->getXRunCount();
    o[13] = xr ? xr.value() : -1;
    // Касание -> звук: момент вывода кадра щелчка (по метке времени потока) минус время касания.
    const int64_t tf = tapFrame_.load();
    if (tf >= 0) {
        int64_t pos = 0, ns = 0;
        if (s->getTimestamp(CLOCK_MONOTONIC, &pos, &ns) == oboe::Result::OK) {
            const double presentNs = double(ns) + double(tf - pos) * 1e9 / s->getSampleRate();
            o[14] = (presentNs - double(tapTouchSeen_.load())) / 1e6;
        }
    }
    o[15] = s->getDeviceId();
    o[16] = s->getChannelCount();
    o[17] = s->getBufferSizeInFrames() * 1000.0 / s->getSampleRate();  // буфер, мс
}
