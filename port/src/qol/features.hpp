#pragma once

#include <filesystem>

/**
 * Quality-of-life changes a datapack switches on (docs/QOL.md): gameplay/qol/features.json, read as the
 * item documents are. Without one every change is off and the game plays as retail.
 */
namespace dc::qol {

/**
 * Which changes are on.
 */
struct Features {
    bool party_pool_restoration = false; /**< Healing water refills the whole party's HP and water. */
    bool inventory_auto_repair = false;  /**< An Auto Repair Powder in the bag saves a breaking weapon too. */
    bool steady_favourite_food = false;  /**< A favourite food's defense is its least for a ranged fighter, its most for melee. */
};

/**
 * Reads the highest-priority usable document, then the data's own, or turns every change off.
 */
void Load();

/**
 * The changes that are on.
 */
const Features &Get();

/**
 * Writes the document as compiled, every change off, under directory/gameplay/qol/. False when it
 * cannot be written.
 */
bool Export(const std::filesystem::path &directory);

} // namespace dc::qol
