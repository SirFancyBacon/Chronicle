#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace dc::game {
struct FishInfo;
class CFish;
class CMainChara;
class CCameraFollow;
} // namespace dc::game

/**
 * The fishing framework's tie to the game: its documents, gameplay/fishing/fish.json, places.json and
 * prizes.json, read as the item documents are, and what retail's fishing code asks of them.
 */
namespace dc::fishing {

struct Place;

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
 * The fish a spot starts with at a time of day (ED_TIME_*), drawn by its place's spawn stage, which
 * becomes the current place. A spot no place is has five empty places, as retail.
 */
Spawning SpawnAt(int spot, int time_of_day);

/**
 * The place fishing last started at (SpawnAt), or for a spot no place is, retail's stages.
 */
const Place &CurrentPlace();

/**
 * The item ID of a bait by its index in esa_info.
 */
std::string_view BaitId(int index);

/**
 * One tick of the angler's session (fishing/angler.cpp), in place of retail's EdMoveChara block:
 * position is where the angler stands; float_weight and hook_weight are how hard the rod holds the
 * float and hook this tick, below 0 for not at all.
 */
void StepSession(game::CMainChara &chara, game::CCameraFollow &camera, float *position, float &float_weight, float &hook_weight);

/**
 * A fish's next action about the bait, when CFish::Step asks: the current place's bite stage's.
 */
void UpdateFishAction(game::CFish &fish);

} // namespace dc::fishing
