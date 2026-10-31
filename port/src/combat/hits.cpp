#include "combat/hits.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <print>
#include <utility>
#include <vector>

#include "data/data_read.hpp"
#include "dcdata.hpp"

namespace dc::combat {

namespace {

struct State {
    std::array<Hit, kHitSlots>                      hits;
    ValueSource                                     source;
    std::map<std::string, std::vector<MotionKey>>   motion_keys;  // by model, as its key table orders them
    std::map<std::string, std::vector<std::string>> motion_names; // the same keys' names
};

State &Get() {
    static State state;
    return state;
}

bool InRange(int slot) {
    return slot >= 0 && slot < kHitSlots;
}

constexpr std::array<std::string_view, 6> kCharacters = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

std::string_view MoveName(Move move) {
    switch (move) {
        case Move::Combo:
            return "combo";
        case Move::Finisher:
            return "finisher";
        case Move::Charged:
            return "charged";
        case Move::Shot:
            return "shot";
        case Move::Thrown:
            return "thrown";
        case Move::Blast:
            return "blast";
        case Move::Trap:
            return "trap";
        case Move::Attack:
            return "attack";
        case Move::Unknown:
            break;
    }
    return "hit";
}

} // namespace

const std::vector<MotionKey> &MotionKeys(const std::string &model) {
    auto &cache = Get().motion_keys;
    if (auto found = cache.find(model); found != cache.end()) {
        return found->second;
    }
    std::vector<MotionKey>    keys;
    std::vector<std::uint8_t> bytes;
    const std::string         path = dcdata::PublishedPath("dun/monstor/" + model + ".game.json",
                                                           [](std::string_view prefix) { return data::DataFileExists(prefix); });
    if (!model.empty() && data::DataReadReadFile(path, bytes)) {
        const nlohmann::json document = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (!document.is_discarded()) {
            const std::string     profile = document.value("default_profile", std::string("info"));
            const nlohmann::json *commands = nullptr;
            if (document.contains("profiles") && document["profiles"].contains(profile)) {
                commands = &document["profiles"][profile]["commands"];
            }
            for (const nlohmann::json &command : commands != nullptr && commands->is_array() ? *commands : nlohmann::json::array()) {
                if (command.value("command", "") != "KEY") {
                    continue;
                }
                // KEY name, first frame, frame after the last, speed.
                const nlohmann::json &args = command["args"];
                auto                  value = [&](std::size_t i) -> const nlohmann::json                  *{
                    return args.is_array() && i < args.size() && args[i].contains("value") ? &args[i]["value"] : nullptr;
                };
                MotionKey key;
                if (const nlohmann::json *name = value(0); name != nullptr && name->is_string()) {
                    key.name = name->get<std::string>();
                }
                if (const nlohmann::json *start = value(1); start != nullptr && start->is_number()) {
                    key.start = start->get<int>();
                }
                if (const nlohmann::json *end = value(2); end != nullptr && end->is_number()) {
                    key.end = end->get<int>();
                }
                keys.push_back(std::move(key));
            }
        }
    }
    return cache.emplace(model, std::move(keys)).first->second;
}

const std::vector<std::string> &MotionNames(const std::string &model) {
    auto &cache = Get().motion_names;
    if (auto found = cache.find(model); found != cache.end()) {
        return found->second;
    }
    std::vector<std::string> names;
    for (const MotionKey &key : MotionKeys(model)) {
        names.push_back(key.name);
    }
    return cache.emplace(model, std::move(names)).first->second;
}

void SetValueSource(ValueSource source) {
    Get().source = std::move(source);
}

void Reset(int slot) {
    if (InRange(slot)) {
        Get().hits[static_cast<std::size_t>(slot)] = Hit{};
    }
}

void Describe(int slot, Hit hit) {
    if (!InRange(slot)) {
        return;
    }
    State          &state = Get();
    const HitValues values = state.source ? state.source(hit) : HitValues{};
    hit.poise = values.poise;
    hit.stamina = values.stamina;
    state.hits[static_cast<std::size_t>(slot)] = std::move(hit);
}

const Hit *Find(int slot) {
    return InRange(slot) ? &Get().hits[static_cast<std::size_t>(slot)] : nullptr;
}

std::string AttackKey(int motion, std::string_view frame, std::span<const std::string> names) {
    if (motion < 0) {
        return std::string(frame);
    }
    const bool named = motion >= 0 && static_cast<std::size_t>(motion) < names.size() && !names[static_cast<std::size_t>(motion)].empty();
    return std::format("{}/{}", named ? names[static_cast<std::size_t>(motion)] : std::to_string(motion), frame);
}

int MotionIndex(const std::string &model, std::string_view name) {
    const std::vector<std::string> &names = MotionNames(model);
    const auto                      found = std::ranges::find(names, name);
    return found == names.end() ? -1 : static_cast<int>(found - names.begin());
}

std::string AttackKey(const Hit &hit) {
    return AttackKey(hit.motion, hit.frame, MotionNames(hit.model));
}

std::string_view CharacterName(int character) {
    return character >= 0 && character < static_cast<int>(kCharacters.size()) ? kCharacters[static_cast<std::size_t>(character)] : std::string_view("character");
}

std::string Name(const Hit &hit) {
    switch (hit.attacker) {
        case Attacker::Character: {
            const std::string_view who = CharacterName(hit.who);
            return hit.step > 0 ? std::format("{} {} ({})", who, MoveName(hit.move), hit.step) : std::format("{} {}", who, MoveName(hit.move));
        }
        case Attacker::Monster:
            return std::format("{}[{}] {}", hit.model, hit.who, AttackKey(hit));
        case Attacker::Item:
            return std::format("item {}", MoveName(hit.move));
        case Attacker::Bomb:
            return "bomb";
        case Attacker::Floor:
            return "trap";
        case Attacker::None:
            break;
    }
    return "undescribed hit";
}

bool Tracing() {
    static const bool on = std::getenv("DC_COMBAT_TRACE") != nullptr;
    return on;
}

void Trace(std::string_view line) {
    if (Tracing()) {
        std::println(stderr, "combat-trace: {}", line);
    }
}

void TraceLanded(int slot, std::string_view target, int damage) {
    const Hit *hit = Find(slot);
    if (!Tracing() || hit == nullptr) {
        return;
    }
    std::println(stderr, "combat-trace: {} -> {}: {} dmg, {} poise, {} stamina", Name(*hit), target, damage, hit->poise, hit->stamina);
}

} // namespace dc::combat
