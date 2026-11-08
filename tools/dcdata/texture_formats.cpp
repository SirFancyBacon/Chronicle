#include "texture_formats.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "dcdata.hpp"
#include "kazarov_bridge/rust/lib.h"
#include "ktx2.hpp"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace dcdata {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json; // keeps a document's keys in the order the converter wrote them

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && EqualsFolded(text.substr(text.size() - suffix.size()), suffix);
}

std::string WithExtension(std::string_view path, std::string_view from, std::string_view to) {
    return std::string(path.substr(0, path.size() - from.size())) + std::string(to);
}

// Every "levels" list in a document: a picture's, and each of its palette variants'.
void ForEachLevel(Json &node, const std::function<void(Json &)> &visit) {
    if (node.is_object()) {
        for (auto &[key, value] : node.items()) {
            if (key == "levels" && value.is_array()) {
                for (Json &level : value) {
                    if (level.is_string()) {
                        visit(level);
                    }
                }
            } else {
                ForEachLevel(value, visit);
            }
        }
    } else if (node.is_array()) {
        for (Json &item : node) {
            ForEachLevel(item, visit);
        }
    }
}

Json ReadDocument(const fs::path &path) {
    const std::vector<unsigned char> bytes = ReadFile(path);
    return Json::parse(bytes.begin(), bytes.end());
}

void WriteDocument(const fs::path &path, const Json &document) {
    const std::string text = document.dump(2) + "\n";
    WriteFile(path, std::span(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}

std::vector<fs::path> Documents(const fs::path &textures) {
    std::vector<fs::path> documents;
    for (const auto &entry : fs::recursive_directory_iterator(textures)) {
        if (entry.is_regular_file() && EndsWith(entry.path().filename().string(), ".json")) {
            documents.push_back(entry.path());
        }
    }
    std::ranges::sort(documents);
    return documents;
}

// Runs `work` on each of `count` items over `jobs` threads, and throws the first failure once all have stopped.
void Share(std::size_t count, unsigned jobs, const std::function<void(std::size_t)> &work) {
    std::atomic<std::size_t> next = 0;
    std::exception_ptr       failure;
    std::mutex               failure_lock;
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < std::max(1u, jobs); i++) {
        threads.emplace_back([&] {
            for (std::size_t item = next++; item < count; item = next++) {
                try {
                    work(item);
                } catch (...) {
                    const std::scoped_lock hand(failure_lock);
                    failure = failure ? failure : std::current_exception();
                    next = count;
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

std::uint32_t Be32(const std::vector<unsigned char> &bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
}

std::vector<std::uint8_t> DecodePng(const fs::path &path, std::uint32_t &width, std::uint32_t &height) {
    const std::vector<unsigned char> png = ReadFile(path);
    static constexpr unsigned char kSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png.size() < 24 || !std::equal(std::begin(kSignature), std::end(kSignature), png.begin()) ||
        std::string_view(reinterpret_cast<const char *>(png.data()) + 12, 4) != "IHDR") {
        throw std::runtime_error("not a PNG: " + path.string());
    }
    width = Be32(png, 16);
    height = Be32(png, 20);
    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        throw std::runtime_error("PNG of an unusable size: " + path.string());
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    kazarov::png_decode_rgba(rust::Slice<const std::uint8_t>(png.data(), png.size()), width, height,
                             rust::Slice<std::uint8_t>(rgba.data(), rgba.size()));
    return rgba;
}

std::vector<unsigned char> EncodePng(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t> &rgba) {
    std::vector<unsigned char> png;
    const auto                 append = [](void *context, void *data, int size) {
        auto *out = static_cast<std::vector<unsigned char> *>(context);
        out->insert(out->end(), static_cast<unsigned char *>(data), static_cast<unsigned char *>(data) + size);
    };
    if (stbi_write_png_to_func(append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), static_cast<int>(width) * 4) == 0) {
        throw std::runtime_error("cannot write a PNG");
    }
    return png;
}

} // namespace

std::size_t PackTexturesAsKtx2(const fs::path &textures, unsigned jobs) {
    if (!fs::is_directory(textures)) {
        return 0;
    }
    const std::vector<fs::path>    documents = Documents(textures);
    std::vector<Json>              parsed;
    std::set<fs::path>             unique;
    for (const fs::path &document : documents) {
        parsed.push_back(ReadDocument(document));
        ForEachLevel(parsed.back(), [&](Json &level) {
            const std::string name = level.get<std::string>();
            if (EndsWith(name, ".png")) {
                unique.insert((document.parent_path() / name).lexically_normal());
            }
        });
    }
    const std::vector<fs::path> pngs(unique.begin(), unique.end());
    Share(pngs.size(), jobs, [&](std::size_t index) {
        const fs::path           &png = pngs[index];
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        const auto                rgba = DecodePng(png, width, height);
        const auto                ktx2 = ktx2::Encode(width, height, rgba);
        std::uint32_t             back_width = 0;
        std::uint32_t             back_height = 0;
        std::vector<std::uint8_t> back;
        std::string               why;
        if (ktx2.empty() || !ktx2::Decode(ktx2, back_width, back_height, back, &why) || back_width != width || back_height != height || back != rgba) {
            throw std::runtime_error("a KTX2 of " + png.string() + " doesn't give its pixels back" + (why.empty() ? "" : ": " + why));
        }
        fs::path out = png;
        out.replace_extension(".ktx2");
        WriteFile(out, ktx2);
    });
    for (std::size_t i = 0; i < documents.size(); i++) {
        bool changed = false;
        ForEachLevel(parsed[i], [&](Json &level) {
            const std::string name = level.get<std::string>();
            if (EndsWith(name, ".png")) {
                level = WithExtension(name, ".png", ".ktx2");
                changed = true;
            }
        });
        if (changed) {
            WriteDocument(documents[i], parsed[i]);
        }
    }
    for (const fs::path &png : pngs) {
        fs::remove(png);
    }
    return pngs.size();
}

void ExportPng(const fs::path &data, const fs::path &out, unsigned jobs) {
    if (fs::exists(out)) {
        throw std::runtime_error("export folder already exists: " + out.string());
    }
    const fs::path textures = data / "textures";
    if (!fs::is_directory(textures)) {
        throw std::runtime_error("no baked textures in " + data.string());
    }
    fs::create_directories(out);
    // Textures: each KTX2 a document names becomes a PNG, the document pointing at it; everything else is copied.
    std::set<fs::path> named;
    for (const fs::path &document : Documents(textures)) {
        Json parsed = ReadDocument(document);
        ForEachLevel(parsed, [&](Json &level) {
            const std::string name = level.get<std::string>();
            if (EndsWith(name, ".ktx2")) {
                named.insert((document.parent_path() / name).lexically_normal());
                level = WithExtension(name, ".ktx2", ".png");
            }
        });
        WriteDocument(out / document.lexically_relative(data), parsed);
    }
    const std::vector<fs::path> ktx2s(named.begin(), named.end());
    Share(ktx2s.size(), jobs, [&](std::size_t index) {
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        std::vector<std::uint8_t> rgba;
        std::string               why;
        if (!ktx2::Decode(ReadFile(ktx2s[index]), width, height, rgba, &why)) {
            throw std::runtime_error(ktx2s[index].string() + ": " + why);
        }
        fs::path png = out / ktx2s[index].lexically_relative(data);
        png.replace_extension(".png");
        WriteFile(png, EncodePng(width, height, rgba));
    });
    std::size_t copied = 0;
    for (const auto &entry : fs::recursive_directory_iterator(data)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const fs::path    relative = entry.path().lexically_relative(data);
        const std::string name = entry.path().filename().string();
        const bool        texture = *relative.begin() == "textures";
        const bool        model = EndsWith(name, ".gltf") || EndsWith(name, ".gltf.bin") || EndsWith(name, ".game.json") || relative == "models.json";
        if ((texture && !EndsWith(name, ".json") && !EndsWith(name, ".ktx2")) || (!texture && model)) {
            fs::create_directories((out / relative).parent_path());
            fs::copy_file(entry.path(), out / relative);
            copied++;
        }
    }
    Log(stdout, "exported {} textures as PNG and copied {} model and other texture files into {}\n", ktx2s.size(), copied, out.string());
}

void RequireSamePictures(const std::vector<std::pair<fs::path, fs::path>> &png_ktx2, unsigned jobs) {
    Share(png_ktx2.size(), jobs, [&](std::size_t index) {
        const auto &[png, ktx2] = png_ktx2[index];
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        const auto                old_rgba = DecodePng(png, width, height);
        std::uint32_t             new_width = 0;
        std::uint32_t             new_height = 0;
        std::vector<std::uint8_t> new_rgba;
        if (!ktx2::Decode(ReadFile(ktx2), new_width, new_height, new_rgba) || new_width != width || new_height != height || new_rgba != old_rgba) {
            throw std::runtime_error("existing texture differs from what this conversion bakes in its place; preserving it: " + png.string());
        }
    });
}

bool IsPngFormOf(const fs::path &old_document, const fs::path &new_document) {
    try {
        Json old_form = ReadDocument(old_document);
        ForEachLevel(old_form, [](Json &level) {
            const std::string name = level.get<std::string>();
            if (EndsWith(name, ".png")) {
                level = WithExtension(name, ".png", ".ktx2");
            }
        });
        // Compared as values: the converter and dcdata may lay the same document out differently.
        return nlohmann::json(old_form) == nlohmann::json(ReadDocument(new_document));
    } catch (const nlohmann::json::exception &) {
        return false;
    }
}

} // namespace dcdata
