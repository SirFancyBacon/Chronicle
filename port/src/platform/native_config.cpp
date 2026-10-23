#include "platform/native_config.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "platform/datapacks.hpp"

namespace native_config {

std::vector<std::shared_ptr<const Json> > Candidates(std::string_view relative, std::vector<std::string> &names) {
    std::vector<std::shared_ptr<const Json> > documents;
    names.clear();
    for (const auto &candidate : datapacks::GameplayCandidates(relative)) {
        std::vector<std::uint8_t> bytes;
        auto parsed = vfs::Read(candidate, bytes) ? Json::parse(bytes.begin(), bytes.end(), nullptr, false)
                                                  : Json(Json::value_t::discarded);
        if (parsed.is_discarded()) {
            Report(candidate.Display(), "configuration is not JSON");
            continue;
        }
        documents.push_back(std::make_shared<const Json>(std::move(parsed)));
        names.push_back(candidate.Display());
    }
    return documents;
}

void CheckKind(const Json &document, std::string_view kind) {
    if (!document.is_object()) {
        throw std::runtime_error("configuration is not an object");
    }
    const auto version = document.find("schema_version");
    if (version == document.end() || !version->is_number_integer()) {
        throw std::runtime_error("unsupported configuration schema");
    }
    if (version->get<long long>() != kSchemaVersion) {
        throw std::runtime_error("schema_version " + version->dump() + " is not this version's " +
                                 std::to_string(kSchemaVersion));
    }
    const auto found = document.find("kind");
    if (found == document.end() || !found->is_string() || found->get_ref<const std::string &>() != kind) {
        throw std::runtime_error("configuration is not a " + std::string(kind) + " document");
    }
}

void Report(std::string_view path, std::string_view error) {
    std::fprintf(stderr, "config: %.*s: %.*s\n", static_cast<int>(path.size()), path.data(),
                 static_cast<int>(error.size()), error.data());
}

const Json &At(const Json &object, const char *key) {
    if (!object.is_object()) {
        throw std::runtime_error(std::string("expected an object holding ") + key);
    }
    const auto found = object.find(key);
    if (found == object.end()) {
        throw std::runtime_error(std::string("missing ") + key);
    }
    return *found;
}

int Int(const Json &value) {
    if (!value.is_number_integer()) {
        throw std::runtime_error("expected an integer");
    }
    if (value.is_number_unsigned()) {
        if (value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("integer out of range");
        }
        return static_cast<int>(value.get<std::uint64_t>());
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        throw std::runtime_error("integer out of range");
    }
    return static_cast<int>(number);
}

int Int(const Json &object, const char *key) {
    return Int(At(object, key));
}

int Int(const Json &object, const char *key, long long low, long long high) {
    const int value = Int(object, key);
    if (value < low || value > high) {
        throw std::runtime_error(std::string(key) + " " + std::to_string(value) + " is outside " + std::to_string(low) +
                                 " to " + std::to_string(high));
    }
    return value;
}

float Float(const Json &value) {
    if (!value.is_number()) {
        throw std::runtime_error("expected a number");
    }
    const auto number = static_cast<float>(value.get<double>());
    if (!std::isfinite(number)) {
        throw std::runtime_error("non-finite number");
    }
    return number;
}

float Float(const Json &object, const char *key) {
    return Float(At(object, key));
}

const Json &Array(const Json &object, const char *key) {
    const auto &value = At(object, key);
    if (!value.is_array()) {
        throw std::runtime_error(std::string(key) + " is not an array");
    }
    return value;
}

} // namespace native_config
