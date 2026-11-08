#pragma once

#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

namespace dcdata {

/**
 * Packs every picture level the documents under `textures` name as a PNG into a KTX2 file beside it (ktx2.hpp), points the
 * documents at those, and removes the PNGs. Each is checked to unpack to the PNG's exact pixels first. A PNG no document
 * names (a memory card icon a glTF uses) stays. Returns how many levels it packed.
 */
std::size_t PackTexturesAsKtx2(const std::filesystem::path &textures, unsigned jobs);

/**
 * Writes the baked textures under `data` to `out` as PNG, their documents pointing at those, along with the models' glTF
 * bundles and models.json, whose images then open: the files a modder works from. `out` mustn't exist yet.
 */
void ExportPng(const std::filesystem::path &data, const std::filesystem::path &out, unsigned jobs);

/**
 * Throws, naming the first, if any pair's PNG (first) and KTX2 (second) aren't the same picture, texel for texel: a level
 * baked before as PNG that a bake now replaces must be what it bakes, not something edited since.
 */
void RequireSamePictures(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> &png_ktx2, unsigned jobs);

/** Whether `old_document` is `new_document` as baked before textures were KTX2: the same, but for its levels being PNG. */
bool IsPngFormOf(const std::filesystem::path &old_document, const std::filesystem::path &new_document);

} // namespace dcdata
