#include "sound/soundtrack.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
#include <vector>

#include "kazarov/log.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "sound/trace.hpp"


namespace dc::sound {

namespace na = noktus::audio;

namespace {

constexpr std::size_t kKept = 2;
constexpr std::size_t kMostBytes = 64 * 1024 * 1024;
constexpr std::size_t kMostWaiting = 16;
constexpr std::size_t kMostFailed = 256;

std::filesystem::path Folder() {
    return PathsSaveRoot().parent_path() / "soundtrack";
}

struct Entry {
    std::filesystem::path file;
    float                 gain = 1.0f;
    bool                  loop = true;
    double                loop_start = 0.0;
    double                loop_end = 0.0;
};

// soundtrack.json's entries; anything unreadable told and left out.
std::map<std::string, Entry> ReadMapping(const std::filesystem::path &folder) {
    std::map<std::string, Entry> entries;
    const std::filesystem::path  path = folder / "soundtrack.json";
    std::ifstream                file(path);
    Trace("soundtrack: reading %s (%s)", path.string().c_str(), file ? "found" : "missing");
    if (!file) {
        return entries;
    }
    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false, true);
    if (root.is_discarded() || !root.is_object() || !root.contains("version") || !root["version"].is_number_integer() || root["version"] != 1 ||
        !root.contains("overrides") || !root["overrides"].is_object()) {
        kz::Logf(kz::LogLevel::Warning, "soundtrack", "{} needs version 1 and an overrides object", path.string());
        return entries;
    }
    const auto finite = [](const nlohmann::json &value, const char *key) {
        return !value.contains(key) || (value[key].is_number() && std::isfinite(value[key].get<double>()));
    };
    for (const auto &[name, value] : root["overrides"].items()) {
        if (!value.is_object() || !value.contains("file") || !value["file"].is_string()) {
            kz::Logf(kz::LogLevel::Warning, "soundtrack", "{} needs a \"file\"", name);
            continue;
        }
        if (!finite(value, "gain") || !finite(value, "loop_start") || !finite(value, "loop_end") ||
            (value.contains("loop") && !value["loop"].is_boolean())) {
            kz::Logf(kz::LogLevel::Warning, "soundtrack", "{} has invalid playback settings", name);
            continue;
        }
        entries[name] = {.file = folder / PathsFromUtf8(value["file"].get<std::string>()),
                         .gain = value.value("gain", 1.0f),
                         .loop = value.value("loop", true),
                         .loop_start = value.value("loop_start", 0.0),
                         .loop_end = value.value("loop_end", 0.0)};
    }
    return entries;
}

// A recording's file read and decoded, its entry's settings on it; null, and told why, where it can't be.
std::shared_ptr<const Recording> Load(const Entry &entry) {
    std::ifstream file(entry.file, std::ios::binary);
    std::string   error = "cannot open the file";
    if (file) {
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (auto samples = DecodeRecording(bytes, entry.file.extension().string(), error)) {
            const auto frames = [&](double seconds) { return static_cast<std::size_t>(std::max(0.0, seconds * samples->rate)); };
            auto       recording = std::make_shared<Recording>();
            recording->loop_start = frames(entry.loop_start);
            recording->loop_end = frames(entry.loop_end);
            recording->loop = entry.loop;
            recording->gain = entry.gain;
            recording->samples = std::move(samples);
            return recording;
        }
    }
    kz::Logf(kz::LogLevel::Warning, "soundtrack", "{}: {}", entry.file.string(), error);
    return nullptr;
}

std::size_t BytesOf(const Recording &recording) {
    return recording.samples != nullptr ? recording.samples->data.size() * sizeof(float) : 0;
}

// The recordings asked for, loaded one at a time on a worker: the waiting, those ready (the most recently asked kept) and those that failed.
class Loader {
public:
    ~Loader() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::shared_ptr<const Recording> Get(const std::string &sequence) {
        std::scoped_lock lock(mutex_);
        if (const auto ready = ready_.find(sequence); ready != ready_.end()) {
            ready->second.asked = ++asks_;
            return ready->second.recording;
        }
        if (failed_.contains(sequence) || loading_ == sequence || std::ranges::find(waiting_, sequence) != waiting_.end() ||
            waiting_.size() >= kMostWaiting) {
            return nullptr;
        }
        waiting_.push_back(sequence);
        if (!worker_.joinable()) {
            worker_ = std::thread([this] { Work(); });
        }
        wake_.notify_one();
        return nullptr;
    }

private:
    struct Ready {
        std::shared_ptr<const Recording> recording;
        std::uint64_t                    asked = 0;
    };

    void Work() {
        std::map<std::string, Entry> mapping;
        bool                         read = false;
        std::unique_lock             lock(mutex_);
        while (true) {
            wake_.wait(lock, [&] { return stopping_ || !waiting_.empty(); });
            if (stopping_) {
                return;
            }
            loading_ = waiting_.front();
            waiting_.pop_front();
            const std::string sequence = loading_;
            lock.unlock();
            if (!read) {
                mapping = ReadMapping(Folder());
                read = true;
            }
            const auto                       found = mapping.find(sequence);
            std::shared_ptr<const Recording> recording = found != mapping.end() ? Load(found->second) : nullptr;
            lock.lock();
            loading_.clear();
            if (recording == nullptr) {
                if (failed_.size() >= kMostFailed) {
                    failed_.clear();
                }
                failed_.insert(sequence);
                continue;
            }
            ready_[sequence] = {.recording = std::move(recording), .asked = ++asks_};
            Trim();
        }
    }

    // The least recently asked go first, past two or 64 MiB; one playing keeps its samples until it's done.
    void Trim() {
        const auto total = [&] {
            std::size_t bytes = 0;
            for (const auto &[name, ready] : ready_) {
                bytes += BytesOf(*ready.recording);
            }
            return bytes;
        };
        while (ready_.size() > 1 && (ready_.size() > kKept || total() > kMostBytes)) {
            const auto oldest = std::ranges::min_element(ready_, {}, [](const auto &pair) { return pair.second.asked; });
            ready_.erase(oldest);
        }
    }

    std::mutex                   mutex_;
    std::condition_variable      wake_;
    std::thread                  worker_;
    bool                         stopping_ = false;
    std::deque<std::string>      waiting_;
    std::string                  loading_;
    std::map<std::string, Ready> ready_;
    std::set<std::string>        failed_;
    std::uint64_t                asks_ = 0;
};

Loader &Recordings() {
    static Loader loader;
    return loader;
}

} // namespace

std::shared_ptr<na::Samples> DecodeRecording(std::span<const std::uint8_t> bytes, std::string_view extension_name, std::string &error) {
    std::string extension(extension_name);
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".wav" && extension != ".flac" && extension != ".ogg") {
        error = "unsupported type (use .wav, .flac or .ogg)";
        return nullptr;
    }
    std::string                  why;
    std::shared_ptr<na::Samples> samples = na::DecodeRecording(bytes, &why);
    if (samples == nullptr) {
        error = "cannot decode the file (" + why + ")";
        return nullptr;
    }
    // Played all around as music is: one channel on both sides.
    if (samples->channels == 1) {
        std::vector<float> both(samples->data.size() * 2);
        for (std::size_t i = 0; i < samples->data.size(); i++) {
            both[i * 2] = both[i * 2 + 1] = samples->data[i];
        }
        samples->data = std::move(both);
        samples->channels = 2;
    }
    return samples;
}

bool SoundtrackEnsure() {
    const std::filesystem::path folder = Folder();
    const std::filesystem::path path = folder / "soundtrack.json";
    std::error_code             error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        kz::Logf(kz::LogLevel::Error, "soundtrack", "cannot create {}: {}", folder.string(), error.message());
        return false;
    }
    if (std::filesystem::exists(path, error)) {
        return true;
    }
    if (error) {
        kz::Logf(kz::LogLevel::Error, "soundtrack", "cannot check {}: {}", path.string(), error.message());
        return false;
    }
    std::ofstream file(path);
    file << "{\n  \"version\": 1,\n  \"overrides\": {}\n}\n";
    file.close();
    if (!file) {
        kz::Logf(kz::LogLevel::Error, "soundtrack", "cannot write {}", path.string());
        return false;
    }
    return true;
}

std::shared_ptr<const Recording> RecordingFor(std::string_view sequence) {
    if (!SettingsGet().soundtrack) {
        return nullptr;
    }
    return Recordings().Get(std::string(sequence));
}

} // namespace dc::sound
