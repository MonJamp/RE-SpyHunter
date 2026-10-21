#include "PckArchive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

namespace spyhunter::runtime
{
namespace
{
constexpr std::uint32_t magic = 0xba773265;
constexpr std::uint32_t version = 0x03000042; // Archive header in this release.

bool read32(std::ifstream& stream, std::uint32_t& value)
{
    std::array<unsigned char, 4> bytes{};
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return false;
    value = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
            (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    return true;
}

bool readString(std::ifstream& stream, std::string& value, const std::uint32_t maximum)
{
    std::uint32_t length = 0;
    if (!read32(stream, length) || length < 2 || length > maximum) return false;
    value.resize(length);
    if (!stream.read(value.data(), length) || value.back() != '\0') return false;
    value.pop_back();
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; });
}

bool readPrecedingLoadName(std::ifstream& stream, const std::uint64_t offset, std::string& value)
{
    // Level archives store each requested load path as a length-prefixed string
    // ending exactly at the following serialized object header.
    const auto earliest = offset > 516 ? offset - 516 : 0;
    for (auto start = offset >= 6 ? offset - 6 : 0; start >= earliest; --start) {
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(start));
        std::string candidate;
        if (readString(stream, candidate, 512) && stream.tellg() == static_cast<std::streamoff>(offset)) {
            value = std::move(candidate);
            return true;
        }
        if (start == 0) break;
    }
    return false;
}

bool parseEntry(std::ifstream& stream, const std::uint64_t offset, const std::uint64_t fileSize, PckEntry& entry)
{
    if (offset + 48 > fileSize) return false;
    readPrecedingLoadName(stream, offset, entry.loadName);
    stream.clear(); stream.seekg(static_cast<std::streamoff>(offset));
    std::uint32_t marker = 0, entryVersion = 0, count = 0, pluginCount = 0, zero = 0;
    std::string plugins;
    if (!read32(stream, marker) || marker != magic || !read32(stream, entryVersion) ||
        (entryVersion & 0xff000000) != 0x03000000 || !read32(stream, count) || count != 4 ||
        !readString(stream, plugins, 32) || plugins != "vkPlugIns" ||
        !read32(stream, entryVersion) || (entryVersion & 0xff000000) != 0x03000000 ||
        !read32(stream, pluginCount) || pluginCount > 32) return false;
    for (std::uint32_t i = 0; i < pluginCount; ++i) {
        std::string field;
        for (int j = 0; j < 4; ++j) if (!readString(stream, field, 256)) return false;
        if (!read32(stream, count) || !read32(stream, zero) || zero != 0 ||
            !read32(stream, entryVersion)) return false;
    }
    if (!readString(stream, entry.type, 128) ||
        !read32(stream, entryVersion) || (entryVersion & 0xff000000) != 0x03000000) return false;
    entry.serializationVersion = entryVersion;
    const auto nameRegion = stream.tellg();
    if (nameRegion < 0) return false;
    bool foundName = false;
    for (std::uint64_t displacement = 0; displacement < 256 &&
         static_cast<std::uint64_t>(nameRegion) + displacement + 8 < fileSize; ++displacement) {
        stream.clear(); stream.seekg(nameRegion + static_cast<std::streamoff>(displacement));
        std::string candidate;
        if (!readString(stream, candidate, 256)) continue;
        const auto dot = candidate.find_last_of('.');
        if (dot == std::string::npos || dot + 2 > candidate.size() || candidate.size() - dot > 9) continue;
        if (!std::all_of(candidate.begin() + static_cast<std::ptrdiff_t>(dot + 1), candidate.end(),
                         [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                     (c >= '0' && c <= '9'); })) continue;
        entry.name = std::move(candidate);
        foundName = true;
        break;
    }
    if (!foundName) {
        if (entry.loadName.empty()) return false;
        const auto separator = entry.loadName.find_last_of("/\\");
        entry.name = entry.loadName.substr(separator == std::string::npos ? 0 : separator + 1);
        if (entry.name.empty()) return false;
    }
    const auto position = stream.tellg();
    if (position < 0 || static_cast<std::uint64_t>(position) > fileSize) return false;
    entry.offset = offset;
    return true;
}
}

bool PckArchive::open(const std::filesystem::path& path, std::string& error, const std::stop_token stop)
{
    path_.clear(); sourceName_.clear(); entries_.clear(); error.clear();
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) { error = "Could not open PCK file."; return false; }
    const auto end = stream.tellg();
    if (end < 16) { error = "PCK file is too short."; return false; }
    const auto fileSize = static_cast<std::uint64_t>(end);
    stream.seekg(0);
    std::uint32_t header = 0, release = 0, count = 0;
    if (!read32(stream, header) || header != magic || !read32(stream, release) || release != version ||
        !read32(stream, count) || count != 1 || !readString(stream, sourceName_, 4096)) {
        error = "Unsupported or damaged PCK header."; return false;
    }
    const auto first = stream.tellg();
    if (first < 0) { error = "PCK header is truncated."; return false; }
    // Scan in fixed-size blocks. Validate the complete serialized record before accepting a marker.
    constexpr std::size_t blockSize = 65536;
    std::array<unsigned char, blockSize + 3> block{};
    std::size_t carry = 0;
    std::uint64_t position = static_cast<std::uint64_t>(first);
    while (position < fileSize) {
        if (stop.stop_requested()) { error = "PCK indexing cancelled."; entries_.clear(); return false; }
        const auto amount = static_cast<std::size_t>((std::min)(std::uint64_t(blockSize), fileSize - position));
        stream.clear(); stream.seekg(static_cast<std::streamoff>(position));
        if (!stream.read(reinterpret_cast<char*>(block.data() + carry), amount)) {
            error = "Could not scan PCK entries."; entries_.clear(); return false;
        }
        const auto total = carry + amount;
        const auto base = position - carry;
        for (std::size_t i = 0; i + 4 <= total; ++i) {
            if (block[i] != 0x65 || block[i+1] != 0x32 || block[i+2] != 0x77 || block[i+3] != 0xba) continue;
            PckEntry entry;
            if (parseEntry(stream, base + i, fileSize, entry)) entries_.push_back(std::move(entry));
        }
        carry = (std::min)(std::size_t{3}, total);
        std::copy_n(block.data() + total - carry, carry, block.data());
        position += amount;
    }
    if (entries_.empty() || entries_.front().offset != static_cast<std::uint64_t>(first)) {
        error = "No valid PCK entry at the end of the archive header.";
        entries_.clear(); return false;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i)
        entries_[i].size = (i + 1 < entries_.size() ? entries_[i+1].offset : fileSize) - entries_[i].offset;
    path_ = path;
    return true;
}

std::size_t PckArchive::readEntry(const std::size_t index, const std::uint64_t offset, void* destination,
                                  const std::size_t capacity, std::string& error) const
{
    error.clear();
    if (index >= entries_.size() || offset > entries_[index].size || (!destination && capacity)) {
        error = "Invalid PCK entry read range."; return 0;
    }
    const auto amount = static_cast<std::size_t>((std::min)(std::uint64_t(capacity), entries_[index].size - offset));
    if (!amount) return 0;
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) { error = "Could not reopen PCK file."; return 0; }
    stream.seekg(static_cast<std::streamoff>(entries_[index].offset + offset));
    stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(amount));
    const auto received = static_cast<std::size_t>(stream.gcount());
    if (received != amount) error = "PCK entry read ended early.";
    return received;
}
}
