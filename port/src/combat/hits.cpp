#include "combat/hits.hpp"

#include <nlohmann/json.hpp>

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
    std::map<std::string, std::vector<std::string>> motion_names; // by model, as its key table orders them
};

State &Get() {
    static State state;
    return state;
}

bool InRange(int slot) {
    return slot >= 0 && slot < kHitSlots;
}

// A monster's motion names, in its key table's order: the KEY commands of its model's sidecar. Retail's
// own model script numbers its motions only, so the names come from the extracted data, read once a model.
const std::vector<std::string> &MotionNames(const std::string &model) {
    auto &cache = Get().motion_names;
    if (auto found = cache.find(model); found != cache.end()) {
        return found->second;
    }
    std::vector<std::string>  names;
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
                const nlohmann::json &args = command["args"];
                const bool            named = args.is_array() && !args.empty() && args[0].contains("value") && args[0]["value"].is_string();
                names.push_back(named ? args[0]["value"].get<std::string>() : std::string());
            }
        }
    }
    return cache.emplace(model, std::move(names)).first->second;
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

void TraceLanded(int slot, std::string_view target, int damage) {
    const Hit *hit = Find(slot);
    if (!Tracing() || hit == nullptr) {
        return;
    }
    std::println(stderr, "combat-trace: {} -> {}: {} dmg, {} poise, {} stamina", Name(*hit), target, damage, hit->poise, hit->stamina);
}

} // namespace dc::combat
