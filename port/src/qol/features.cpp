#include "qol/features.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <print>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "platform/native_config.hpp"
#include "platform/paths.hpp"

namespace dc::qol {

using Json = nlohmann::json;
namespace native_config = platform::native_config;

namespace {

constexpr const char *kPath = "qol/features.json";
constexpr const char *kKind = "qol_features";

Features g_features;

// The document's switches, each a member of Features by its name.
constexpr std::pair<const char *, bool Features::*> kSwitches[] = {
    {"party_pool_restoration", &Features::party_pool_restoration},
    {"inventory_auto_repair",  &Features::inventory_auto_repair },
    {"steady_favourite_food",  &Features::steady_favourite_food },
};

Features Parse(const Json &document, std::set<std::string> &unknown) {
    Features features;
    for (const auto &[name, member] : kSwitches) {
        if (const auto found = document.find(name); found != document.end()) {
            if (!found->is_boolean()) {
                throw std::runtime_error(std::format("{} must be true or false", name));
            }
            features.*member = found->get<bool>();
        }
    }
    for (const auto &[key, value] : document.items()) {
        const bool known = key == "schema_version" || key == "kind" ||
                           std::ranges::any_of(kSwitches, [&key](const auto &entry) { return key == entry.first; });
        if (!known) {
            unknown.insert(key);
        }
    }
    return features;
}

} // namespace

void Load() {
    g_features = {};
    std::set<std::string> unknown;
    native_config::Read(kPath, kKind, [&](const Json &document) {
        unknown.clear();
        g_features = Parse(document, unknown);
    });
    if (!unknown.empty()) {
        std::string names;
        for (const std::string &name : unknown) {
            names += names.empty() ? name : ", " + name;
        }
        native_config::Report(std::format("gameplay/{}", kPath), "fields this version does not read: " + names);
    }
}

const Features &Get() {
    return g_features;
}

bool Export(const std::filesystem::path &directory) {
    Json document = {
        {"schema_version", native_config::kSchemaVersion},
        {"kind",           kKind                        },
    };
    const Features off;
    for (const auto &[name, member] : kSwitches) {
        document[name] = off.*member;
    }
    const std::filesystem::path file = directory / "gameplay" / kPath;
    std::error_code             error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << document.dump(2) << '\n';
    if (error || !stream) {
        std::println(stderr, "cannot write {}", PathsDisplay(file));
        return false;
    }
    return true;
}

} // namespace dc::qol
