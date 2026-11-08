#pragma once

// Baked textures as KTX2 (Khronos's container for GPU textures): one level of RGBA8 in sRGB, compressed with Zstandard,
// which loses nothing. Shared by dcdata, which writes them, and the game, which reads them; both link zstd.
// A PNG was the baked form before: decoding one cost the game's thread about six times what this does.

#include <zstd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace dcdata::ktx2 {

inline constexpr std::array<std::uint8_t, 12> kIdentifier = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
inline constexpr std::uint32_t kR8G8B8A8Unorm = 37;
inline constexpr std::uint32_t kR8G8B8A8Srgb = 43;
inline constexpr std::uint32_t kSupercompressionNone = 0;
inline constexpr std::uint32_t kSupercompressionZstd = 2;
// The level dcdata writes at: slow to write once, and as quick to read as any other.
inline constexpr int kZstdLevel = 19;

namespace detail {

constexpr std::size_t kHeaderBytes = 80;     // identifier, nine header words, then the index
constexpr std::size_t kLevelIndexBytes = 24; // byte offset, byte length and uncompressed length of a level
constexpr std::size_t kDfdBytes = 92;        // its total size, then one basic descriptor block of four samples

inline void Put32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

inline void Put64(std::vector<std::uint8_t> &out, std::uint64_t value) {
    for (int i = 0; i < 8; i++) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

inline std::uint32_t Get32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; i--) {
        value = (value << 8) | bytes[at + static_cast<std::size_t>(i)];
    }
    return value;
}

inline std::uint64_t Get64(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint64_t>(Get32(bytes, at)) | (static_cast<std::uint64_t>(Get32(bytes, at + 4)) << 32);
}

// The Khronos Data Format descriptor for R8G8B8A8_SRGB: RGBSDA, BT.709 primaries, sRGB transfer, straight alpha, one
// byte a channel, alpha linear as the sRGB formats have it.
inline void PutDfd(std::vector<std::uint8_t> &out) {
    Put32(out, static_cast<std::uint32_t>(kDfdBytes));
    Put32(out, 0);                 // vendor Khronos, basic descriptor
    Put32(out, (88u << 16) | 2u);  // block size, version 1.3
    Put32(out, 1u | (1u << 8) | (2u << 16)); // RGBSDA, BT.709, sRGB, no flags
    Put32(out, 0);                 // a 1x1x1x1 texel block
    Put32(out, 4);                 // four bytes in plane 0
    Put32(out, 0);
    constexpr std::array<std::uint32_t, 4> channels = {0, 1, 2, 15 | 0x10}; // R, G, B, then alpha, linear
    for (std::uint32_t i = 0; i < 4; i++) {
        Put32(out, (i * 8) | (7u << 16) | (channels[i] << 24));
        Put32(out, 0);
        Put32(out, 0);
        Put32(out, 255);
    }
}

} // namespace detail

/** Whether these bytes begin as a KTX2 file does. */
inline bool IsKtx2(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= kIdentifier.size() && std::memcmp(bytes.data(), kIdentifier.data(), kIdentifier.size()) == 0;
}

/** A KTX2 file of one level, `width` by `height` RGBA8 texels in sRGB, Zstandard at `level`. Empty when zstd fails. */
inline std::vector<std::uint8_t> Encode(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba, int level = kZstdLevel) {
    const std::size_t texels = static_cast<std::size_t>(width) * height * 4;
    if (width == 0 || height == 0 || rgba.size() != texels) {
        return {};
    }
    std::vector<std::uint8_t> packed(ZSTD_compressBound(texels));
    const std::size_t         packed_size = ZSTD_compress(packed.data(), packed.size(), rgba.data(), rgba.size(), level);
    if (ZSTD_isError(packed_size) != 0) {
        return {};
    }
    const std::string writer = "KTXwriter";
    const std::string tool = "Chronicle dcdata";
    const std::size_t kvd_entry = writer.size() + 1 + tool.size() + 1;
    const std::size_t kvd_bytes = 4 + kvd_entry + ((4 - kvd_entry % 4) % 4);
    const std::size_t dfd_offset = detail::kHeaderBytes + detail::kLevelIndexBytes;
    const std::size_t kvd_offset = dfd_offset + detail::kDfdBytes;
    const std::size_t level_offset = kvd_offset + kvd_bytes; // a supercompressed level needs no alignment

    std::vector<std::uint8_t> out(kIdentifier.begin(), kIdentifier.end());
    out.reserve(level_offset + packed_size);
    detail::Put32(out, kR8G8B8A8Srgb);
    detail::Put32(out, 1); // type size
    detail::Put32(out, width);
    detail::Put32(out, height);
    detail::Put32(out, 0); // depth: a 2D texture
    detail::Put32(out, 0); // layers: not an array
    detail::Put32(out, 1); // faces
    detail::Put32(out, 1); // levels
    detail::Put32(out, kSupercompressionZstd);
    detail::Put32(out, static_cast<std::uint32_t>(dfd_offset));
    detail::Put32(out, static_cast<std::uint32_t>(detail::kDfdBytes));
    detail::Put32(out, static_cast<std::uint32_t>(kvd_offset));
    detail::Put32(out, static_cast<std::uint32_t>(kvd_bytes));
    detail::Put64(out, 0); // no supercompression global data
    detail::Put64(out, 0);
    detail::Put64(out, level_offset);
    detail::Put64(out, packed_size);
    detail::Put64(out, texels);
    detail::PutDfd(out);
    detail::Put32(out, static_cast<std::uint32_t>(kvd_entry));
    out.insert(out.end(), writer.begin(), writer.end());
    out.push_back(0);
    out.insert(out.end(), tool.begin(), tool.end());
    out.push_back(0);
    out.resize(level_offset, 0);
    out.insert(out.end(), packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(packed_size));
    return out;
}

/**
 * The first level of a KTX2 file as RGBA8, for the kinds Encode writes: R8G8B8A8, Zstandard or none. False, with why in
 * `error`, for anything else or anything damaged.
 */
inline bool Decode(std::span<const std::uint8_t> bytes, std::uint32_t &width, std::uint32_t &height, std::vector<std::uint8_t> &rgba,
                   std::string *error = nullptr) {
    const auto fail = [&](const char *why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    if (!IsKtx2(bytes) || bytes.size() < detail::kHeaderBytes + detail::kLevelIndexBytes) {
        return fail("not a KTX2 file");
    }
    const std::uint32_t format = detail::Get32(bytes, 12);
    if (format != kR8G8B8A8Srgb && format != kR8G8B8A8Unorm) {
        return fail("KTX2 format isn't R8G8B8A8");
    }
    width = detail::Get32(bytes, 20);
    height = detail::Get32(bytes, 24);
    const std::uint32_t scheme = detail::Get32(bytes, 44);
    if (width == 0 || height == 0 || width > 16384 || height > 16384 || detail::Get32(bytes, 28) > 1 || detail::Get32(bytes, 32) > 1 ||
        detail::Get32(bytes, 36) != 1) {
        return fail("KTX2 file isn't one 2D picture");
    }
    if (scheme != kSupercompressionZstd && scheme != kSupercompressionNone) {
        return fail("KTX2 file is compressed some other way than Zstandard");
    }
    const std::uint64_t offset = detail::Get64(bytes, detail::kHeaderBytes);
    const std::uint64_t length = detail::Get64(bytes, detail::kHeaderBytes + 8);
    const std::size_t   texels = static_cast<std::size_t>(width) * height * 4;
    if (offset > bytes.size() || length > bytes.size() - offset) {
        return fail("KTX2 level lies past the file's end");
    }
    const std::span<const std::uint8_t> level = bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
    // Checked before anything's made to unpack into, so a damaged header can't ask for a gigabyte.
    if (scheme == kSupercompressionZstd && ZSTD_getFrameContentSize(level.data(), level.size()) != texels) {
        return fail("KTX2 level doesn't unpack to the picture's size");
    }
    if (scheme == kSupercompressionNone && level.size() != texels) {
        return fail("KTX2 level isn't the picture's size");
    }
    rgba.resize(texels);
    if (scheme == kSupercompressionNone) {
        std::memcpy(rgba.data(), level.data(), texels);
        return true;
    }
    const std::size_t unpacked = ZSTD_decompress(rgba.data(), rgba.size(), level.data(), level.size());
    if (ZSTD_isError(unpacked) != 0 || unpacked != texels) {
        return fail("KTX2 level doesn't unpack to the picture's size");
    }
    return true;
}

} // namespace dcdata::ktx2
