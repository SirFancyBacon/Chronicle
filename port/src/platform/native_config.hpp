#pragma once

#include <nlohmann/json.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Gameplay documents (schema 2) read through datapack priority: the candidates for a path are tried
// from the highest priority down, and the first whose schema, kind and contents parse is used. One that
// fails prints "config: <path>: <reason>" and the next is tried. Brought across from ChronicleTwo's
// port with its names (docs/ITEM_FRAMEWORK.md, section 7).
namespace native_config {

using Json = nlohmann::json;

// The schema version of every gameplay document, ChronicleTwo's native documents' own.
inline constexpr int kSchemaVersion = 2;

// The parsed JSON of every candidate of a path under gameplay/, highest priority first, with their names.
std::vector<std::shared_ptr<const Json> > Candidates(std::string_view relative, std::vector<std::string> &names);

// Throws unless a document is an object of this schema version with the requested kind.
void CheckKind(const Json &document, std::string_view kind);

// Prints one diagnostic for a document that could not be used.
void Report(std::string_view path, std::string_view error);

// Parses the highest-priority candidate of a path whose envelope and contents are valid.
template <typename T>
std::optional<T> Read(std::string_view relative, std::string_view kind, const std::function<T(const Json &)> &parse) {
    std::vector<std::string> names;
    const auto               documents = Candidates(relative, names);
    for (std::size_t index = 0; index < documents.size(); ++index) {
        try {
            CheckKind(*documents[index], kind);
            return parse(*documents[index]);
        } catch (const std::exception &exception) {
            Report(names[index], exception.what());
        }
    }
    if (documents.empty()) {
        Report(relative, "gameplay document missing");
    }
    return std::nullopt;
}

// A required member of an object.
const Json &At(const Json &object, const char *key);

// A 32-bit integer.
int Int(const Json &value);

// A required integer member.
int Int(const Json &object, const char *key);

// A required integer member that lies in [low, high].
int Int(const Json &object, const char *key, long long low, long long high);

// A finite number as a float.
float Float(const Json &value);

// A required float member.
float Float(const Json &object, const char *key);

// A required array member.
const Json &Array(const Json &object, const char *key);

} // namespace native_config
