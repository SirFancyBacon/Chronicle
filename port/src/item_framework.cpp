#include "item_framework.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Items {
namespace {

struct State {
    // Owned one by one, so a Def a caller holds stays where it is while others register.
    std::map<int, std::unique_ptr<Def> >         by_number;
    std::map<std::string, const Def *, std::less<> > by_id;
};

State &Get() {
    static State state;
    return state;
}

bool ValidPart(std::string_view part) {
    return !part.empty() && std::ranges::all_of(part, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

} // namespace

bool ValidId(std::string_view id) {
    const std::size_t colon = id.find(':');
    return colon != std::string_view::npos && ValidPart(id.substr(0, colon)) && ValidPart(id.substr(colon + 1));
}

void Register(Def item) {
    State &state = Get();
    if (!ValidId(item.id)) {
        throw std::logic_error("item ID \"" + item.id + "\" is not namespace:name");
    }
    if (state.by_id.contains(item.id)) {
        throw std::logic_error("item \"" + item.id + "\" is registered twice");
    }
    if (state.by_number.contains(item.number)) {
        throw std::logic_error("item \"" + item.id + "\" has number " + std::to_string(item.number) + ", which \"" +
                               state.by_number.at(item.number)->id + "\" has");
    }
    if (item.Has<Weapon>() && (item.Has<Consumable>() || item.Has<Attachment>())) {
        throw std::logic_error("item \"" + item.id + "\" is a weapon and cannot also be consumed or attached");
    }
    if (const QuickSlot *slot = item.Get<QuickSlot>(); slot != nullptr && (slot->max < 1 || slot->max > kQuickSlotMaxLimit)) {
        throw std::logic_error("item \"" + item.id + "\" has a quick-slot maximum outside 1 to " +
                               std::to_string(kQuickSlotMaxLimit));
    }
    auto owned = std::make_unique<Def>(std::move(item));
    state.by_id.emplace(owned->id, owned.get());
    const int number = owned->number;
    state.by_number.emplace(number, std::move(owned));
}

void Clear() {
    State &state = Get();
    state.by_id.clear();
    state.by_number.clear();
}

const Def *Find(std::string_view id) {
    const State &state = Get();
    const auto   found = state.by_id.find(id);
    return found != state.by_id.end() ? found->second : nullptr;
}

const Def *Find(int number) {
    const State &state = Get();
    const auto   found = state.by_number.find(number);
    return found != state.by_number.end() ? found->second.get() : nullptr;
}

int NumberOf(std::string_view id) {
    const Def *item = Find(id);
    return item != nullptr ? item->number : -1;
}

std::vector<const Def *> All() {
    std::vector<const Def *> items;
    for (const auto &[number, item] : Get().by_number) {
        items.push_back(item.get());
    }
    return items;
}

} // namespace Items
