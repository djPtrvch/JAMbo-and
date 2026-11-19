// Room (Headphone Monitor, фаза 2; порт Core/Monitor/RoomIR.swift, ветка swift JAMbo): бинауральные импульсы комнат —
// Room*.wav и rooms.json. WAV: 48 кГц, 4 канала — LL, LR, RL, RR, 16/24/32 бит или float. Прямая часть — первые direct
// отсчётов (на 0 dB всегда), остальное — хвост (громкость — Ambience).
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace jambo {

/// Комната из rooms.json.
struct RoomInfo {
    std::string name, file;
    double gainDb = 0;
    int direct = 128;
    std::string headFile;
    double headFrom = 0, headStep = 2, headGainDb = 0;
    std::string flatFile;
    double flatGainDb = 0;
    std::string flatHeadFile;
    double flatHeadGainDb = 0;
    std::string space;
    std::string headName = "FABIAN";
    std::string dir;
    double hpHz = 0;
    int hpOrder = 0;

    /// Список из rooms.json ({"rooms": […]}); ошибка — пусто.
    static std::vector<std::shared_ptr<RoomInfo>> list(const std::string& json);
};

/// Отклик комнаты на частотах компенсации: |D|², Re(D·T*), |T|² прямой части D и хвоста T.
struct RoomResponse {
    std::vector<double> dd, dt, tt;
    explicit RoomResponse(int count) : dd(size_t(count), 0), dt(size_t(count), 0), tt(size_t(count), 0) {}
    double power(int k, double amb) const {
        return dd[size_t(k)] + 2 * amb * dt[size_t(k)] + amb * amb * tt[size_t(k)];
    }
    /// Срез низа колонок (Баттерворт порядка order на hz): |H|² по точкам усреднения.
    void highpass(double hz, int order);
};

/// Импульсы комнаты: 4 пути (LL, LR, RL, RR), 48 кГц.
class RoomIR {
public:
    static constexpr int maxFrames = 52800;
    RoomIR(std::string name, std::vector<float> paths, int frames, int direct)
        : name(std::move(name)), frames(frames), direct(direct), h(std::move(paths)) {}
    /// Из WAV (4 канала, 48 кГц); неверный файл — nullptr.
    static std::shared_ptr<RoomIR> fromWav(const std::string& name, const std::vector<uint8_t>& wav, double gainDb,
                                           int direct);
    const std::string name;
    const int frames;
    const int direct;
    /// Пути подряд: [путь · frames + i].
    const std::vector<float> h;

    /// EAR TONE: flat + raw · (self − flat).
    std::shared_ptr<RoomIR> mixed(const RoomIR* flat, double raw) const;
    struct Wav {
        int channels, rate, frames;
        std::vector<float> samples;
    };
    static std::optional<Wav> parseWav(const std::vector<uint8_t>& d);
    /// Отклик для Level compensation.
    RoomResponse response(double sampleRate) const;
};

}  // namespace jambo
