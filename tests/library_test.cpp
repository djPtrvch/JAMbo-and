// Библиотека и файлы сэмплов (Android): дерево (Library), JSON как у iPad, SHA-256, WAV/AIFF/CAF, пересчёт частоты,
// SampleStore на диске, высота (PitchDetect).
#include "files/AudioFile.h"
#include "files/SampleStore.h"
#include "files/Sha256.h"
#include "model/Library.h"
#include "sampler/Pitch.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace jambo;

namespace {
int fails = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        std::printf("%-58s FAIL\n", what.c_str());
        ++fails;
    }
}

void sha() {
    check(Sha256::key("abc", 3) == "ba7816bf8f01cfea414140de", "sha256 abc");
    check(Sha256::key("", 0) == "e3b0c44298fc1c149afbf4c8", "sha256 пусто");
    const std::string m(1000, 'a');
    Sha256 s;
    s.update(m.data(), 500);
    s.update(m.data(), 500);
    check(Sha256::hex24(s.finish()) == Sha256::key(m.data(), m.size()), "sha256 по частям");
}

void tree() {
    Library l;
    check(l.nodes.size() == 1 && l.nodes[0].id == Library::recordings, "RECORDINGS");
    l.createFolder(Library::root, "Drums");
    const auto& a = l.addSample(Library::root, "k1", "kick 10.wav");
    l.addSample(Library::root, "k2", "kick 9.wav");
    l.addSample(Library::root, "k1", "dup");
    check(l.children(Library::root).size() == 4, "тот же сэмпл не повторяется");
    const auto c = l.children(Library::root);
    check(c[0].id == Library::recordings && c[1].name == "Drums" && c[2].name == "kick 9.wav" &&
              c[3].name == "kick 10.wav", "порядок: системные, папки, имена по числам");
    check(l.uniqueName(Library::root, "kick 9") == "kick 9 2", "имя без расширения занято");
    check(l.sameName(Library::root, "KICK 10") != nullptr, "sameName без регистра");
    check(Library::clean("   ") == "Untitled", "пустое имя");
    const std::string id = a.id;
    l.rename(id, "snare");
    check(l.node(id)->name == "snare", "rename");
    const std::string drums = l.children(Library::root)[1].id;
    l.addSample(drums, "k3", "hat");
    check(l.countInside(drums) == 1, "countInside");
    l.remove(drums);
    check(l.node(drums) == nullptr && l.nodes.size() == 3, "remove с содержимым");
    l.remove(Library::recordings);
    check(l.node(Library::recordings) != nullptr, "RECORDINGS не удаляется");
    SynthPreset p;
    p.name = "FM 1";
    p.sound = "mi:18";
    l.addPreset(Library::root, p);
    const auto j = LibraryCoding::encode(l);
    const auto back = LibraryCoding::decode(j);
    check(back && back->nodes.size() == l.nodes.size() && back->nodes.back().preset &&
              back->nodes.back().preset->sound == "mi:18", "JSON туда и обратно");
    check(LibraryCoding::decode("{\"nodes\":[],\"marks\":{}}")->nodes.size() == 1, "JSON без RECORDINGS — добавляется");
}

std::vector<uint8_t> wav16(const std::vector<int16_t>& x, int ch, int rate) {
    std::vector<uint8_t> o;
    auto s = [&](const char* t) { o.insert(o.end(), t, t + 4); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) o.push_back(uint8_t(v >> (8 * i))); };
    auto u16 = [&](uint32_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); };
    s("RIFF"); u32(uint32_t(36 + x.size() * 2)); s("WAVE");
    s("fmt "); u32(16); u16(1); u16(uint32_t(ch)); u32(uint32_t(rate)); u32(uint32_t(rate * ch * 2)); u16(uint32_t(ch * 2)); u16(16);
    s("data"); u32(uint32_t(x.size() * 2));
    for (auto v : x) u16(uint16_t(v));
    return o;
}

void files() {
    std::vector<int16_t> x = {0, 16384, -16384, 32767, -32768, 0};
    const auto w = wav16(x, 2, 48000);
    const auto a = AudioFile::parse(w.data(), w.size());
    check(a && a->frames() == 3 && !a->right.empty() && a->left[1] == -0.5f && a->right[0] == 0.5f, "WAV 16 стерео");
    std::vector<float> l = {0.25f, -1.f, 0.5f}, r = {1.f, 0.f, -0.75f};
    const auto c = AudioFile::caf(l, &r, 48000);
    const auto b = AudioFile::parse(c.data(), c.size());
    check(b && b->rate == 48000 && b->left == l && b->right == r, "CAF float32 туда и обратно");
    const auto wf = AudioFile::wavFloat32(l, nullptr);
    const auto d = AudioFile::parse(wf.data(), wf.size());
    check(d && d->left == l && d->right.empty(), "WAV float32 моно");
    // Синус 1 кГц 44,1 → 48 кГц: амплитуда и частота сохраняются.
    std::vector<float> s(44100);
    for (size_t i = 0; i < s.size(); i++) s[i] = float(0.5 * std::sin(2 * M_PI * 1000 * double(i) / 44100));
    const auto y = Resample::run(s, 44100, 48000);
    double pk = 0, err = 0;
    for (size_t i = 4800; i < 43200; i++) {
        pk = std::max(pk, double(std::fabs(y[i])));
        err = std::max(err, std::fabs(double(y[i]) - 0.5 * std::sin(2 * M_PI * 1000 * double(i) / 48000)));
    }
    check(y.size() == 48000 && std::fabs(pk - 0.5) < 0.002 && err < 0.002, "пересчёт 44,1 → 48 кГц");
}

void store() {
    const std::string dir = (std::filesystem::temp_directory_path() / "jambo_samples_test").string();
    std::filesystem::remove_all(dir);
    SampleStore st(dir);
    std::vector<int16_t> x(48000);
    for (size_t i = 0; i < x.size(); i++) x[i] = int16_t(12000 * std::sin(2 * M_PI * 220 * double(i) / 48000));
    const auto w = wav16(x, 1, 48000);
    const auto l = SampleStore::decode(w.data(), w.size(), "a3.wav", dir);
    check(l && l->key == Sha256::key(w.data(), w.size()) && l->data->frames() == 48000, "decode WAV");
    check(l && l->root && *l->root == 57, "высота A3 — 57");
    const auto again = SampleStore::read(l ? l->key : "", dir);
    check(again && again->frames() == 48000 && std::fabs(again->left()[100] - l->data->left()[100]) < 1e-7f,
          "копия .caf на диске");
    bool need = false;
    const std::string junk = "ID3 not audio";
    check(!SampleStore::decode(reinterpret_cast<const uint8_t*>(junk.data()), junk.size(), "x.mp3", dir, &need) && need,
          "не WAV — декодирует экран");
    std::vector<float> pcm(2 * 22050, 0.25f);
    const auto p = SampleStore::fromPcm("abc123", "x.mp3", pcm.data(), 22050, 2, 22050, dir);
    check(p && p->data->frames() == 48000 && p->data->channels() == 2, "PCM 22,05 кГц стерео → 48 кГц");
    std::filesystem::remove_all(dir);
}
}  // namespace

int runLibraryTests() {
    sha();
    tree();
    files();
    store();
    std::printf("library: %d / %d passed\n", checks - fails, checks);
    return fails;
}
