#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * Dark Cloud's models, motions, skin weights and bind poses are published as glTF bundles with
 * sidecars (docs/MODEL_FRAMEWORK.md). The game's loaders still take the retail records, so the
 * records are rebuilt here from a bundle, byte for byte, in place of the disc's.
 */
namespace dc::data::model_gltf {

/**
 * Whether the published data holds the source file `alias`: a logical path, then the pack members
 * inside it ("chara/c01d.chr/c01d.mds"), or a scene inside a file ("gedit/s26/scene.scn#offset:4016").
 */
bool Publishes(std::string_view alias);

/**
 * The source file `alias` rebuilt from its bundle, or null when the data doesn't publish it or the
 * bundle can't rebuild it. The bytes live until the next call.
 */
const std::vector<std::uint8_t> *Rebuild(std::string_view alias);

/**
 * Writes every published source inside the file `path`, just read into `bytes`, back where the
 * disc has it: pack members (nested packs included) and scenes at offsets, rebuilt from their
 * bundles. A source that doesn't fit its place is left as the file has it, and reported.
 */
void Splice(std::string_view path, std::span<std::uint8_t> bytes);

/**
 * Rebuilds every published source whose alias starts with one of `prefixes`, on a thread of its own, and keeps them for good: the files menus open, so
 * that opening one doesn't wait while dozens of models are rebuilt. A rebuild the game asks for meanwhile waits for one source at most. Called again, it
 * stops the last one first. The data's reader isn't made to be indexed while it's read, so call it once the data is indexed.
 */
void PrepareAhead(std::vector<std::string> prefixes);

/**
 * Stops rebuilding ahead, for good: before the data is indexed again.
 */
void StopPreparing();

/**
 * Forgets the index, the bundles and every rebuilt file, for when the data changes. Stops rebuilding ahead.
 */
void Reset();

} // namespace dc::data::model_gltf
