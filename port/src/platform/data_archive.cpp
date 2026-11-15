#include "data_archive.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#define STBI_SUPPORT_ZLIB
#include "stb_image.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace dc::platform {

namespace {

constexpr std::uint32_t kEndSignature = 0x06054B50;
constexpr std::uint32_t kZip64EndSignature = 0x06064B50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064B50;
constexpr std::size_t   kZip64EndRecordSize = 56;
constexpr std::size_t   kZip64LocatorSize = 20;
constexpr std::uint32_t kSaturated32 = 0xFFFFFFFF;
constexpr std::uint16_t kSaturated16 = 0xFFFF;
constexpr std::uint32_t kCentralSignature = 0x02014B50;
constexpr std::uint32_t kLocalSignature = 0x04034B50;
constexpr std::uint32_t kDescriptorSignature = 0x08074B50;
constexpr std::uint16_t kEncryptedFlag = 1U << 0;
constexpr std::uint16_t kDeflateOptionFlags = (1U << 1) | (1U << 2);
constexpr std::uint16_t kDataDescriptorFlag = 1U << 3;
constexpr std::uint16_t kStrongEncryptionFlag = 1U << 6;
constexpr std::uint16_t kUtf8Flag = 1U << 11;
constexpr std::uint16_t kMaskedHeaderFlag = 1U << 13;
constexpr std::uint16_t kAllowedFlags = kDataDescriptorFlag | kUtf8Flag | kDeflateOptionFlags;
constexpr std::uint16_t kZip64ExtraTag = 1;
constexpr std::size_t   kEndRecordSize = 22;
constexpr std::uint64_t kMaxTailSize = kEndRecordSize + 0xFFFF;
constexpr std::size_t   kLocalHeaderSize = 30;
constexpr std::size_t   kCentralHeaderSize = 46;
constexpr std::size_t   kMaxBulkIndexSize = 16 * 1024 * 1024;

std::uint16_t Read16(std::span<const std::uint8_t> bytes, std::size_t offset = 0) {
    return static_cast<std::uint16_t>(bytes[offset]) | static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset = 0) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t Read64(std::span<const std::uint8_t> bytes, std::size_t offset = 0) {
    return static_cast<std::uint64_t>(Read32(bytes, offset)) | (static_cast<std::uint64_t>(Read32(bytes, offset + 4)) << 32);
}

constexpr std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::size_t value = 0; value < table.size(); ++value) {
        auto crc = static_cast<std::uint32_t>(value);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0U - (crc & 1U)));
        }
        table[value] = crc;
    }
    return table;
}

constexpr std::array<std::array<std::uint32_t, 256>, 8> MakeCrcTables() {
    std::array<std::array<std::uint32_t, 256>, 8> tables{};
    tables[0] = MakeCrcTable();
    for (std::size_t slice = 1; slice < tables.size(); ++slice) {
        for (std::size_t value = 0; value < tables[slice].size(); ++value) {
            const std::uint32_t crc = tables[slice - 1][value];
            tables[slice][value] = tables[0][crc & 0xFF] ^ (crc >> 8);
        }
    }
    return tables;
}

constexpr auto        kCrcTables = MakeCrcTables();
constexpr const auto &kCrcTable = kCrcTables[0];

std::uint32_t Crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFF;
    for (const std::uint8_t byte : bytes) {
        crc = kCrcTable[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

std::uint32_t Crc32Slice8(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFF;
    while (bytes.size() >= 8) {
        const std::uint32_t first = crc ^ Read32(bytes);
        crc = kCrcTables[7][first & 0xFF] ^ kCrcTables[6][(first >> 8) & 0xFF] ^
              kCrcTables[5][(first >> 16) & 0xFF] ^ kCrcTables[4][first >> 24] ^
              kCrcTables[3][bytes[4]] ^ kCrcTables[2][bytes[5]] ^
              kCrcTables[1][bytes[6]] ^ kCrcTables[0][bytes[7]];
        bytes = bytes.subspan(8);
    }
    for (const std::uint8_t byte : bytes) {
        crc = kCrcTable[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

bool CrcSlice8Enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("DC_ARCHIVE_CRC_SLICE8");
        return value == nullptr || std::string_view(value) == "1";
    }();
    return enabled;
}

bool BulkIndexEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("DC_ARCHIVE_BULK_INDEX");
        return value == nullptr || std::string_view(value) == "1";
    }();
    return enabled;
}

bool SequentialIndexEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("DC_ARCHIVE_SEQUENTIAL_INDEX");
        return value == nullptr || std::string_view(value) == "1";
    }();
    return enabled;
}

bool LoadPerfEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("DC_PERF_LOAD");
        return value != nullptr && std::string_view(value) == "1";
    }();
    return enabled;
}

using LoadClock = std::chrono::steady_clock;

double LoadMilliseconds(LoadClock::time_point start, LoadClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool SafeEntryName(std::string_view name) {
    if (name.empty() || name.front() == '/' || name.contains('\\') ||
        name.contains(':') || name.contains('\0')) {
        return false;
    }
    if (name.back() == '/') {
        name.remove_suffix(1);
    }
    if (name.empty()) {
        return false;
    }
    for (std::size_t start = 0; start < name.size();) {
        const std::size_t      slash = name.find('/', start);
        const std::string_view part = name.substr(start, slash == std::string_view::npos ? name.size() - start : slash - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (const unsigned char character : part) {
            if (character < 0x20 || character == 0x7F) {
                return false;
            }
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

[[noreturn]] void Invalid(const std::filesystem::path &path, std::string_view reason) {
    throw std::runtime_error(path.string() + ": " + std::string(reason));
}

std::intptr_t OpenNative(const std::filesystem::path &path) {
#ifdef _WIN32
    // Overlapped, so threads' reads go at once: Windows takes a synchronous handle's reads one at a time.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    return file == INVALID_HANDLE_VALUE ? -1 : reinterpret_cast<std::intptr_t>(file);
#else
    return ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
}

void CloseNative(std::intptr_t handle) {
    if (handle == -1) {
        return;
    }
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(handle));
#else
    ::close(static_cast<int>(handle));
#endif
}

// Positioned reads leave no shared position behind, so threads can read at once.
bool ReadNativeAt(std::intptr_t handle, std::uint64_t offset, std::span<std::uint8_t> output) {
    while (!output.empty()) {
        const std::size_t chunk = std::min<std::size_t>(output.size(), std::size_t{1} << 30);
#ifdef _WIN32
        // Each read its own event to wait on: the file's own handle is signalled by whichever read finishes.
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        at.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (at.hEvent == nullptr) {
            return false;
        }
        const HANDLE file = reinterpret_cast<HANDLE>(handle);
        DWORD        got = 0;
        bool         done = ReadFile(file, output.data(), static_cast<DWORD>(chunk), nullptr, &at) != FALSE;
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            done = true;
        }
        done = done && GetOverlappedResult(file, &at, &got, TRUE) != FALSE;
        CloseHandle(at.hEvent);
        if (!done || got == 0) {
            return false;
        }
        const std::size_t read = got;
#else
        const ssize_t got = ::pread(static_cast<int>(handle), output.data(), chunk, static_cast<off_t>(offset));
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        const auto read = static_cast<std::size_t>(got);
#endif
        output = output.subspan(read);
        offset += read;
    }
    return true;
}

} // namespace

DataArchive::DataArchive(const std::filesystem::path &path)
    : path_(path) {
    // The index is read through a stream, which is let go once it's read; entries are read through the native file, at their offsets.
    std::ifstream stream_(path, std::ios::binary);
    const bool perf_load = LoadPerfEnabled();
    const bool sequential_index = SequentialIndexEnabled();
    const bool bulk_requested = BulkIndexEnabled();
    const auto started = perf_load ? LoadClock::now() : LoadClock::time_point{};
    if (!stream_) {
        Invalid(path_, "cannot open ZIP archive");
    }
    std::error_code     error;
    const std::uint64_t file_size = std::filesystem::file_size(path_, error);
    if (error || file_size < kEndRecordSize) {
        Invalid(path_, "truncated ZIP archive");
    }

    const std::uint64_t       tail_size = std::min(file_size, kMaxTailSize);
    std::vector<std::uint8_t> tail(static_cast<std::size_t>(tail_size));
    stream_.seekg(static_cast<std::streamoff>(file_size - tail_size));
    stream_.read(reinterpret_cast<char *>(tail.data()), static_cast<std::streamsize>(tail.size()));
    if (std::cmp_not_equal(stream_.gcount(), tail.size())) {
        Invalid(path_, "cannot read ZIP trailer");
    }

    std::size_t end_at = std::string::npos;
    for (std::size_t at = tail.size() - kEndRecordSize + 1; at-- > 0;) {
        if (Read32(tail, at) == kEndSignature && at + kEndRecordSize + Read16(tail, at + 20) == tail.size()) {
            end_at = at;
            break;
        }
    }
    if (end_at == std::string::npos) {
        Invalid(path_, "ZIP end record not found");
    }
    const std::span<const std::uint8_t> end = std::span<const std::uint8_t>(tail).subspan(end_at);
    std::uint64_t                       disk = Read16(end, 4);
    std::uint64_t                       central_disk = Read16(end, 6);
    std::uint64_t                       disk_entries = Read16(end, 8);
    std::uint64_t                       total_entries = Read16(end, 10);
    std::uint64_t                       central_size = Read32(end, 12);
    std::uint64_t                       central_offset = Read32(end, 16);
    std::uint64_t                       trailer_offset = file_size - tail_size + end_at;
    // ZIP64: past 4 GiB or 65,535 entries the end record's fields are saturated, and the real ones are in a record the locator before it points to.
    const bool saturated = disk == kSaturated16 || central_disk == kSaturated16 || disk_entries == kSaturated16 ||
                           total_entries == kSaturated16 || central_size == kSaturated32 || central_offset == kSaturated32;
    // Only when saturated: a classic archive's last name or comment may hold the locator's signature by chance. With a comment near its
    // largest, the locator lies before the tail read.
    if (saturated) {
        if (trailer_offset < kZip64LocatorSize) {
            Invalid(path_, "ZIP64 locator missing");
        }
        std::array<std::uint8_t, kZip64LocatorSize> locator{};
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(trailer_offset - kZip64LocatorSize));
        stream_.read(reinterpret_cast<char *>(locator.data()), locator.size());
        if (std::cmp_not_equal(stream_.gcount(), locator.size()) || Read32(locator) != kZip64LocatorSignature) {
            Invalid(path_, "ZIP64 locator missing");
        }
        const std::uint64_t record_offset = Read64(locator, 8);
        const std::uint64_t locator_offset = trailer_offset - kZip64LocatorSize;
        if (Read32(locator, 4) != 0 || Read32(locator, 16) > 1 || record_offset > locator_offset ||
            locator_offset - record_offset < kZip64EndRecordSize) {
            Invalid(path_, "invalid ZIP64 locator");
        }
        std::array<std::uint8_t, kZip64EndRecordSize> record{};
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(record_offset));
        stream_.read(reinterpret_cast<char *>(record.data()), record.size());
        if (std::cmp_not_equal(stream_.gcount(), record.size()) || Read32(record) != kZip64EndSignature) {
            Invalid(path_, "invalid ZIP64 end record");
        }
        disk = Read32(record, 16);
        central_disk = Read32(record, 20);
        disk_entries = Read64(record, 24);
        total_entries = Read64(record, 32);
        central_size = Read64(record, 40);
        central_offset = Read64(record, 48);
        trailer_offset = record_offset;
    }
    if (disk != 0 || central_disk != 0 || disk_entries != total_entries) {
        Invalid(path_, "multi-disk ZIP archives are unsupported");
    }
    central_directory_offset_ = central_offset;
    if (central_offset > trailer_offset || central_size > trailer_offset - central_offset) {
        Invalid(path_, "ZIP central directory is outside the archive");
    }

    if (total_entries > central_size / kCentralHeaderSize) {
        Invalid(path_, "ZIP central directory size mismatch");
    }
    records_.reserve(static_cast<std::size_t>(total_entries));
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(central_offset));
    const bool                          bulk_index = bulk_requested && central_size <= kMaxBulkIndexSize;
    std::unique_ptr<std::istringstream> bulk_stream;
    if (bulk_index) {
        std::string storage(static_cast<std::size_t>(central_size), '\0');
        stream_.read(storage.data(), static_cast<std::streamsize>(storage.size()));
        if (std::cmp_not_equal(stream_.gcount(), storage.size())) {
            Invalid(path_, "cannot read ZIP central directory");
        }
        bulk_stream = std::make_unique<std::istringstream>(std::move(storage), std::ios::binary);
    }
    std::istream &central_stream = bulk_stream ? static_cast<std::istream &>(*bulk_stream) : stream_;
    std::uint64_t consumed = 0;
    for (std::uint64_t index = 0; index < total_entries; ++index) {
        std::array<std::uint8_t, kCentralHeaderSize> header{};
        central_stream.read(reinterpret_cast<char *>(header.data()), header.size());
        if (std::cmp_not_equal(central_stream.gcount(), header.size()) || Read32(header) != kCentralSignature) {
            Invalid(path_, "invalid ZIP central directory entry");
        }
        const std::uint16_t flags = Read16(header, 8);
        const std::uint16_t method = Read16(header, 10);
        const std::uint32_t crc = Read32(header, 16);
        const std::uint32_t compressed_size = Read32(header, 20);
        const std::uint32_t size = Read32(header, 24);
        const std::uint16_t name_size = Read16(header, 28);
        const std::uint16_t extra_size = Read16(header, 30);
        const std::uint16_t comment_size = Read16(header, 32);
        const std::uint16_t entry_disk = Read16(header, 34);
        const std::uint32_t local_offset = Read32(header, 42);
        if (entry_disk != 0) {
            Invalid(path_, "multi-disk ZIP entries are unsupported");
        }
        if ((flags & ~kAllowedFlags) != 0 || (flags & kEncryptedFlag) != 0 || (flags & kStrongEncryptionFlag) != 0 ||
            (flags & kMaskedHeaderFlag) != 0) {
            Invalid(path_, "encrypted or unsupported ZIP flags");
        }
        if (method != std::to_underlying(Method::Stored) && method != std::to_underlying(Method::Deflated)) {
            Invalid(path_, "only stored and deflated ZIP entries are supported");
        }
        if (method == std::to_underlying(Method::Stored) && (flags & kDeflateOptionFlags) != 0) {
            Invalid(path_, "deflate flags on a stored ZIP entry");
        }

        std::string name(name_size, '\0');
        central_stream.read(name.data(), name_size);
        std::vector<std::uint8_t> extra(extra_size);
        central_stream.read(reinterpret_cast<char *>(extra.data()), extra_size);
        std::uint64_t size64 = size;
        std::uint64_t compressed64 = compressed_size;
        std::uint64_t local_offset64 = local_offset;
        bool          zip64_extra = false;
        central_stream.ignore(comment_size);
        if (!central_stream || !SafeEntryName(name)) {
            Invalid(path_, "invalid or unsafe ZIP entry name");
        }
        for (std::size_t at = 0; at < extra.size();) {
            if (extra.size() - at < 4) {
                Invalid(path_, "truncated ZIP extra field");
            }
            const std::uint16_t tag = Read16(extra, at);
            const std::uint16_t length = Read16(extra, at + 2);
            at += 4;
            if (length > extra.size() - at) {
                Invalid(path_, "invalid ZIP extra field");
            }
            // ZIP64: the saturated fields' real values, in this order, each only if saturated.
            if (tag == kZip64ExtraTag) {
                std::size_t field = at;
                const auto  take = [&](std::uint64_t &value) {
                    if (field + 8 > at + length) {
                        Invalid(path_, "truncated ZIP64 extra field");
                    }
                    value = Read64(extra, field);
                    field += 8;
                };
                if (size == kSaturated32) {
                    take(size64);
                }
                if (compressed_size == kSaturated32) {
                    take(compressed64);
                }
                if (local_offset == kSaturated32) {
                    take(local_offset64);
                }
                zip64_extra = true;
            }
            at += length;
        }
        if (!zip64_extra && (size == kSaturated32 || compressed_size == kSaturated32 || local_offset == kSaturated32)) {
            Invalid(path_, "ZIP64 extra field missing");
        }

        consumed += kCentralHeaderSize + name_size + extra_size + comment_size;
        if (consumed > central_size) {
            Invalid(path_, "ZIP central directory size mismatch");
        }
        const bool directory = name.back() == '/';
        Record     record{
            .entry = {.name = std::move(name), .size = size64},
            .crc = crc,
            .compressed_size = compressed64,
            .local_header_offset = local_offset64,
            .method = static_cast<Method>(method),
            .flags = flags
        };
        if (!directory && (local_offset64 >= central_offset || compressed64 > central_offset - local_offset64)) {
            Invalid(path_, "ZIP local entry offset is outside the file area");
        }
        records_.push_back(std::move(record));
        if (!sequential_index) {
            central_stream.clear();
            central_stream.seekg(static_cast<std::streamoff>(bulk_index ? consumed : central_offset + consumed));
        }
    }
    if (consumed != central_size) {
        Invalid(path_, "ZIP central directory size mismatch");
    }
    if (perf_load) {
        const auto finished = LoadClock::now();
        std::println(stderr,
                     "perf load archive index entries {} central_bytes {} sequential_index {} index_ms {:.6f} "
                     "bulk_index {} bulk_fallback_count {} bulk_fallback_reason {}",
                     records_.size(), central_size, static_cast<int>(sequential_index),
                     LoadMilliseconds(started, finished), static_cast<int>(bulk_index),
                     static_cast<int>(bulk_requested && !bulk_index),
                     !bulk_requested ? "disabled" : (bulk_index ? "none" : "central_size_cap"));
    }
    handle_ = OpenNative(path_);
    if (handle_ == -1) {
        Invalid(path_, "cannot open ZIP archive");
    }
}

DataArchive::~DataArchive() {
    CloseNative(handle_);
}

bool DataArchive::ReadAt(std::uint64_t offset, std::span<std::uint8_t> output) const {
    return ReadNativeAt(handle_, offset, output);
}

std::size_t DataArchive::Count() const {
    return records_.size();
}

const DataArchive::Entry &DataArchive::GetEntry(std::size_t index) const {
    return records_.at(index).entry;
}

bool DataArchive::Read(std::size_t index, std::span<std::uint8_t> output) const {
    const bool perf_load = LoadPerfEnabled();
    const auto started = perf_load ? LoadClock::now() : LoadClock::time_point{};
    if (index >= records_.size()) {
        return false;
    }
    Record &record = records_[index];
    if (record.entry.name.back() == '/' || record.entry.size != output.size() ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        record.compressed_size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    // A local header's 32-bit sizes may be saturated, the real ones then in its own ZIP64 field, which the central directory's already gave.
    const auto                matches = [](std::uint32_t local, std::uint64_t central) { return local == kSaturated32 || local == central; };
    std::vector<std::uint8_t> compressed;
    LoadClock::time_point     locked;
    const auto                lock_started = perf_load ? LoadClock::now() : LoadClock::time_point{};
    std::uint64_t             data_offset = 0;
    bool                      crc_known = false;
    {
        std::scoped_lock lock(header_mutex_);
        if (perf_load) {
            locked = LoadClock::now();
        }
        if (!record.local_header_checked) {
            std::array<std::uint8_t, kLocalHeaderSize> local{};
            if (!ReadAt(record.local_header_offset, local) || Read32(local) != kLocalSignature ||
                Read16(local, 6) != record.flags || Read16(local, 8) != std::to_underlying(record.method)) {
                return false;
            }
            const std::uint16_t       local_name_size = Read16(local, 26);
            const std::uint16_t       local_extra_size = Read16(local, 28);
            std::vector<std::uint8_t> named(static_cast<std::size_t>(local_name_size) + local_extra_size);
            if (!ReadAt(record.local_header_offset + kLocalHeaderSize, named)) {
                return false;
            }
            const std::string_view              local_name(reinterpret_cast<const char *>(named.data()), local_name_size);
            const std::span<const std::uint8_t> local_extra = std::span<const std::uint8_t>(named).subspan(local_name_size);
            if (local_name != record.entry.name) {
                return false;
            }
            for (std::size_t at = 0; at < local_extra.size();) {
                if (local_extra.size() - at < 4) {
                    return false;
                }
                const std::uint16_t length = Read16(local_extra, at + 2);
                at += 4;
                if (length > local_extra.size() - at) {
                    return false;
                }
                at += length;
            }
            if ((record.flags & kDataDescriptorFlag) == 0 &&
                (Read32(local, 14) != record.crc ||
                 !matches(Read32(local, 18), record.compressed_size) ||
                 !matches(Read32(local, 22), record.entry.size))) {
                return false;
            }
            const std::uint64_t first_data = record.local_header_offset + kLocalHeaderSize + local_name_size + local_extra_size;
            if (first_data > central_directory_offset_ || record.compressed_size > central_directory_offset_ - first_data) {
                return false;
            }
            record.data_offset = first_data;
            record.local_header_checked = true;
        }
        data_offset = record.data_offset;
        crc_known = record.crc_checked;
    }

    if (record.method == Method::Stored) {
        if (record.compressed_size != record.entry.size || !ReadAt(data_offset, output)) {
            return false;
        }
    } else {
        compressed.resize(static_cast<std::size_t>(record.compressed_size));
        if (!ReadAt(data_offset, compressed)) {
            return false;
        }
    }

    if ((record.flags & kDataDescriptorFlag) != 0) {
        const std::uint64_t descriptor_offset = data_offset + record.compressed_size;
        if (descriptor_offset > central_directory_offset_ || central_directory_offset_ - descriptor_offset < 12) {
            return false;
        }
        // Classic descriptors hold 32-bit sizes, ZIP64's 64-bit ones; either may start with a signature.
        std::array<std::uint8_t, 24> descriptor{};
        const std::uint64_t          available = central_directory_offset_ - descriptor_offset;
        const std::size_t            descriptor_size = static_cast<std::size_t>(std::min<std::uint64_t>(available, descriptor.size()));
        if (!ReadAt(descriptor_offset, std::span(descriptor).first(descriptor_size))) {
            return false;
        }
        const auto classic = [&](std::size_t at) {
            return descriptor_size >= at + 12 && Read32(descriptor, at) == record.crc && Read32(descriptor, at + 4) == record.compressed_size &&
                   Read32(descriptor, at + 8) == record.entry.size;
        };
        const auto wide = [&](std::size_t at) {
            return descriptor_size >= at + 20 && Read32(descriptor, at) == record.crc && Read64(descriptor, at + 4) == record.compressed_size &&
                   Read64(descriptor, at + 12) == record.entry.size;
        };
        const bool signed_descriptor = Read32(descriptor) == kDescriptorSignature;
        if (!classic(0) && !wide(0) && !(signed_descriptor && (classic(4) || wide(4)))) {
            return false;
        }
    }

    const auto io_finished = perf_load ? LoadClock::now() : LoadClock::time_point{};
    auto       inflate_finished = io_finished;
    if (record.method == Method::Deflated) {
        char      empty_output = 0;
        char     *output_data = output.empty() ? &empty_output : reinterpret_cast<char *>(output.data());
        const int decoded = stbi_zlib_decode_noheader_buffer(output_data,
                                                             static_cast<int>(output.size()),
                                                             reinterpret_cast<const char *>(compressed.data()),
                                                             static_cast<int>(compressed.size()));
        if (std::cmp_not_equal(decoded, output.size())) {
            return false;
        }
    }
    if (perf_load && record.method == Method::Deflated) {
        inflate_finished = LoadClock::now();
    }
    const bool slice8 = CrcSlice8Enabled();
    const auto crc_started = perf_load ? LoadClock::now() : LoadClock::time_point{};
    // Once per entry and run: the same file read again (a texture's metadata and then its pixels, an area entered again) needn't be checked again.
    const bool valid = crc_known || (slice8 ? Crc32Slice8(output) : Crc32(output)) == record.crc;
    if (valid && !crc_known) {
        std::scoped_lock lock(header_mutex_);
        record.crc_checked = true;
    }
    if (perf_load && valid) {
        const auto finished = LoadClock::now();
        std::println(stderr,
                     "perf load archive entry {} bytes {} method {} crc_slice8 {} "
                     "total_wall_ms {:.6f} lock_wait_ms {:.6f} IO_wall_ms {:.6f} "
                     "inflate_wall_ms {:.6f} crc_wall_ms {:.6f} name {}",
                     index, output.size(), std::to_underlying(record.method), static_cast<int>(slice8),
                     LoadMilliseconds(started, finished), LoadMilliseconds(lock_started, locked),
                     LoadMilliseconds(locked, io_finished), LoadMilliseconds(io_finished, inflate_finished),
                     LoadMilliseconds(crc_started, finished), record.entry.name);
    }
    return valid;
}

bool DataArchivePresent(const std::filesystem::path &data_root) {
    std::error_code error;
    return std::filesystem::is_regular_file(data_root.parent_path() / "data.zip", error);
}

} // namespace dc::platform
