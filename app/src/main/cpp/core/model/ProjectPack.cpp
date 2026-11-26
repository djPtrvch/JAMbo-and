#include "ProjectPack.h"
#include "ProjectStore.h"
#include "Json.h"
#include "files/SampleStore.h"
#include "synth/Wavetable.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <unistd.h>

namespace jambo {

ProjectAssets projectAssets(const Project& project) {
    ProjectAssets assets;
    const auto sample = [&](const std::string& key) {
        if (SampleStore::validKey(key)) assets.samples.insert(key);
    };
    const auto folder = [&](const Folder& f) {
        for (const auto& item : f.items) {
            if (item.sample) sample(*item.sample);
            if (item.preset && item.preset->grain) sample(*item.preset->grain);
            if (item.preset && item.preset->table) assets.tables.insert(*item.preset->table);
        }
    };
    const auto sound = [&](const TrackSound& s) {
        sample(s.sample);
        if (s.grain) sample(*s.grain);
        folder(s.folder);
        for (const auto& [key, _] : s.slices) sample(key);
        assets.tables.insert(s.table);
    };
    const auto clip = [&](const Clip& c) {
        if (c.src) sound(*c.src);
        for (const auto& page : c.pages)
            for (const auto& step : page)
                if (step.sound) sample(*step.sound);
    };
    const auto track = [&](const Track& t) {
        sample(t.sample);
        sample(t.grain);
        folder(t.folder);
        for (const auto& [key, _] : t.slices) sample(key);
        assets.tables.insert(t.table);
        if (t.home) sound(*t.home);
        for (const auto& c : t.clips) if (c) clip(*c);
        for (const auto& block : t.blocks) if (block.pattern) clip(*block.pattern);
    };
    for (const auto& t : project.tracks) track(t);
    for (const auto& t : project.chanSeqs) track(t);
    if (project.arrange.ref) sample(project.arrange.ref->key);
    return assets;
}

namespace ProjectPack {
namespace fs = std::filesystem;

namespace {
constexpr uint32_t maxHeader = 64U * 1024U * 1024U;
constexpr auto magic = std::array<char, 4>{'J', 'M', 'B', '1'};

struct Media {
    std::string key, name;
    fs::path path;
    bool wavetable;
};

std::vector<Media> mediaFor(const Project& project, const Storage& storage) {
    const auto refs = projectAssets(project);
    std::vector<Media> media;
    for (const auto& k : refs.samples) media.push_back({k, k + ".caf", storage.samples / (k + ".caf"), false});
    std::set<std::string> waves;
    for (const auto& k : refs.tables)
        if (const auto table = WavetableKey::parseUser(k)) waves.insert(table->wav);
    for (const auto& k : waves) media.push_back({k, k + ".wav", storage.tables / (k + ".wav"), true});
    return media;
}

std::optional<uint64_t> fileBytes(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return std::nullopt;
    const auto n = fs::file_size(path, ec);
    return ec || n == 0 ? std::nullopt : std::optional<uint64_t>(n);
}

bool copy(std::istream& in, std::ostream& out, uint64_t bytes) {
    std::array<char, 64 * 1024> buffer;
    while (bytes) {
        const auto n = std::streamsize(std::min<uint64_t>(bytes, buffer.size()));
        if (!in.read(buffer.data(), n) || !out.write(buffer.data(), n)) return false;
        bytes -= uint64_t(n);
    }
    return true;
}

struct TempFile {
    fs::path path;
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

struct Part {
    std::string key, name;
    uint64_t offset, bytes;
};

bool install(std::ifstream& in, const Part& part, const Media& media) {
    std::error_code ec;
    if (!part.bytes || part.offset > uint64_t(std::numeric_limits<std::streamoff>::max())) return false;
    if (media.wavetable && part.bytes > WtTable::maxUserBytes) return false;

    fs::create_directories(media.path.parent_path(), ec);
    if (ec) return false;
    std::string candidate = media.path.string() + ".import-XXXXXX";
    const int fd = ::mkstemp(candidate.data());
    if (fd < 0) return false;
    ::close(fd);
    TempFile staging{candidate};
    in.clear();
    in.seekg(std::streamoff(part.offset));
    std::ofstream out(staging.path, std::ios::binary | std::ios::trunc);
    if (!in || !out || !copy(in, out, part.bytes)) return false;
    out.close();
    if (!out) return false;

    if (media.wavetable) {
        if (!WtTable::readUserWave(staging.path)) return false;
    } else {
        std::array<char, 4> tag{};
        std::ifstream probe(staging.path, std::ios::binary);
        if (!probe.read(tag.data(), tag.size())) return false;
        probe.close();
        const auto audio = SampleStore::readFile(staging.path.string());
        if (!audio) return false;
        // Своя копия уже CAF; WAV/AIFF из прежних упаковщиков приводим к формату хранилища.
        if (tag != std::array<char, 4>{'c', 'a', 'f', 'f'}) {
            TempFile conversion{staging.path.string() + ".tmp"};
            if (!SampleStore::write(*audio, staging.path.string())) return false;
        }
    }
    // Атомарно без замены файла, который могла добавить другая операция.
    fs::create_hard_link(staging.path, media.path, ec);
    return !ec || fileBytes(media.path).has_value();
}
}  // namespace

std::optional<size_t> write(const fs::path& path, const std::string& name,
                            const Project& project, const Storage& storage) {
    auto media = mediaFor(project, storage);
    std::vector<std::pair<fs::path, uint64_t>> contents;
    Json samples = Json::array();
    for (const auto& entry : media) {
        if (const auto n = fileBytes(entry.path)) {
            samples.push_back(Json{{"key", entry.key}, {"name", entry.name}, {"size", *n}});
            contents.emplace_back(entry.path, *n);
        }
    }
    const Json header{{"format", ProjectFormat::name}, {"version", ProjectFormat::version}, {"name", name},
                      {"doc", json::put(project)}, {"samples", std::move(samples)}};
    const auto data = header.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (data.size() > maxHeader) return std::nullopt;
    const auto n = uint32_t(data.size());
    const unsigned char be[] = {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    TempFile staged{path.string() + ".writing"};
    std::ofstream out(staged.path, std::ios::binary | std::ios::trunc);
    if (!out.write(magic.data(), magic.size()) ||
        !out.write(reinterpret_cast<const char*>(be), sizeof(be)) ||
        !out.write(data.data(), std::streamsize(data.size()))) return std::nullopt;
    for (const auto& [src, size] : contents) {
        std::ifstream in(src, std::ios::binary);
        if (!copy(in, out, size)) return std::nullopt;
    }
    out.close();
    if (!out) return std::nullopt;
    std::error_code ec;
    fs::rename(staged.path, path, ec);
    if (ec) return std::nullopt;
    return media.size() - contents.size();
}

std::optional<Imported> read(const fs::path& path, const Storage& storage) {
    const auto size = fileBytes(path);
    if (!size || *size < 8) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    std::array<char, 4> tag{};
    unsigned char be[4]{};
    if (!in.read(tag.data(), tag.size()) || tag != magic ||
        !in.read(reinterpret_cast<char*>(be), sizeof(be))) return std::nullopt;
    const uint32_t n = uint32_t(be[0]) << 24 | uint32_t(be[1]) << 16 | uint32_t(be[2]) << 8 | uint32_t(be[3]);
    if (n > maxHeader || n > *size - 8) return std::nullopt;
    std::string bytes(n, '\0');
    if (!in.read(bytes.data(), std::streamsize(n))) return std::nullopt;
    const auto head = Json::parse(bytes, nullptr, false);
    if (!head.is_object() || json::value(head, "format", std::string{}) != ProjectFormat::name) return std::nullopt;
    const auto doc = head.find("doc");
    if (doc == head.end() || !doc->is_object()) return std::nullopt;
    Imported result;
    result.name = json::value(head, "name", std::string{});
    // Как ProjectFormat: те же правила Codable, затем миграции и repair.
    if (!json::get(*doc, result.project)) return std::nullopt;
    result.project.repair();

    uint64_t offset = 8 + uint64_t(n);
    std::map<std::string, std::vector<Part>> parts;
    const auto list = head.find("samples");
    if (list != head.end()) {
        if (!list->is_array()) return std::nullopt;
        for (const auto& item : *list) {
            Part p;
            long long count;
            const auto length = item.find("size");
            if (!json::req(item, "key", p.key) || length == item.end() || !json::getInt64(*length, count) ||
                count < 0 || uint64_t(count) > *size - offset) return std::nullopt;
            p.name = json::value(item, "name", std::string{});
            p.offset = offset;
            p.bytes = uint64_t(count);
            offset += p.bytes;
            if (SampleStore::validKey(p.key)) parts[p.key].push_back(std::move(p));
        }
    }
    for (const auto& entry : mediaFor(result.project, storage)) {
        if (fileBytes(entry.path)) continue;
        const auto found = parts.find(entry.key);
        bool saved = false;
        if (found != parts.end()) {
            const auto& candidates = found->second;
            const auto preferred = std::find_if(candidates.begin(), candidates.end(), [&](const Part& part) {
                return part.name == entry.name;
            });
            if (preferred != candidates.end()) saved = install(in, *preferred, entry);
            if (!saved) for (const auto& part : candidates) {
                if (&part != (preferred == candidates.end() ? nullptr : &*preferred) && install(in, part, entry)) {
                    saved = true;
                    break;
                }
            }
        }
        if (!saved) ++result.missing;
    }
    return result;
}

}  // namespace ProjectPack
}  // namespace jambo
