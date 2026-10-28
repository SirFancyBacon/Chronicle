#pragma once

#include <filesystem>
#include <vector>

namespace dc::game {
struct FishInfo;
}

/**
 * The fishing framework's tie to the game: its documents, gameplay/fishing/fish.json, places.json and
 * prizes.json, read as the item documents are, and what retail's fishing code asks of them.
 */
namespace dc::fishing {

/**
 * Fills the species, places and prizes from the highest-priority usable document, then the data's own,
 * or as compiled. item::Load runs first: species name baits by item ID.
 */
void Load();

/**
 * Writes the documents as compiled under directory/gameplay/fishing/. False when one cannot be
 * written.
 */
bool Export(const std::filesystem::path &directory);

/**
 * A fish kind's row as CFish keeps it, likings in esa_info's order; null for a kind no species is.
 */
const game::FishInfo *Info(int kind);

/**
 * The model archive of a fish kind (GetFishFileName), or null.
 */
char *ModelPath(int kind);

/**
 * The system message naming a fish kind (GetFishMsgNo), 0 for a kind no species is, as retail.
 */
int NameMessage(int kind);

/**
 * Whether a catch of the kind counts toward the Mardan Garayan.
 */
bool CountsAsMardanGarayan(int kind);

/**
 * What fishing at a spot starts with.
 */
struct Spawning {
    std::vector<int> kinds; /**< Each fish's kind; -1 leaves its place empty. */
    bool             draw_under_water = true;
};

/**
 * The fish a spot starts with at a time of day (ED_TIME_*), drawn by its place's spawn stage. A spot no
 * place is has five empty places, as retail.
 */
Spawning SpawnAt(int spot, int time_of_day);

} // namespace dc::fishing
