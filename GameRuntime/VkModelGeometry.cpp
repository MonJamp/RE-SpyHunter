#include "VkModelGeometry.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>

namespace spyhunter::runtime
{
namespace
{
constexpr std::string_view stripsType = "vkPS2TriStrips";
constexpr std::uint32_t minFlagStreamVersion = 0x0300003b; // 3.0.59
constexpr std::size_t readWindow = 64 * 1024;

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}

float readFloat(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return std::bit_cast<float>(readU32(bytes, offset));
}

bool checkedProduct(const std::uint64_t a, const std::uint64_t b, std::uint64_t& product)
{
    if (a && b > (std::numeric_limits<std::uint64_t>::max)() / a) return false;
    product = a * b;
    return true;
}

struct StripRecord
{
    std::size_t typeOffset = 0;
    std::size_t bodyOffset = 0;
    std::uint32_t objectIndex = 0;
    std::uint32_t version = 0;
};

std::vector<StripRecord> findStripRecords(const std::vector<std::uint8_t>& bytes, std::uint32_t maximumRecords, std::stop_token stop)
{
    std::vector<StripRecord> records;
    const std::size_t nameLength = stripsType.size() + 1;
    for (std::size_t at = 0; at + nameLength + 12 <= bytes.size(); ++at) {
        if ((at % readWindow) == 0 && stop.stop_requested()) return {};
        if (bytes[at] != 'v' || bytes[at + 1] != 'k' ||
            !std::equal(stripsType.begin(), stripsType.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at)) ||
            bytes[at + stripsType.size()] != 0) continue;
        if (at < 8 || readU32(bytes, at - 4) != nameLength) continue;
        const auto version = readU32(bytes, at + nameLength);
        if ((version & 0xff000000) != 0x03000000 || version < minFlagStreamVersion) continue;
        const auto nameLengthOffset = at + nameLength + 4;
        const auto entryNameLength = readU32(bytes, nameLengthOffset);
        if (entryNameLength > 4096 || entryNameLength > bytes.size() - nameLengthOffset - 4) continue;
        if (entryNameLength && bytes[nameLengthOffset + 4 + entryNameLength - 1] != 0) continue;
        const auto objectIndex = readU32(bytes, at - 8);
        if (objectIndex > 65535) continue;
        if (records.size() >= maximumRecords) return {};
        records.push_back({at, nameLengthOffset + 4 + entryNameLength, objectIndex, version});
    }
    return records;
}

bool parseVertexStream(const std::vector<std::uint8_t>& bytes, const std::size_t start,
                       const std::size_t end, const StripRecord& record,
                       const VkModelLoadOptions& options, const std::uint64_t decodedSoFar,
                       VkModelDrawable& result, std::stop_token stop)
{
    if (start > end || end > bytes.size() || end - start < 12) return false;
    const auto count = readU32(bytes, start);
    const auto flags = readU32(bytes, start + 4);
    if (count < 3 || count > options.maxVerticesPerDrawable || (flags & ~0x0fu) != 0) return false;

    const bool hasFlag2 = (flags & 0x2) != 0;
    const bool hasFlag4 = (flags & 0x4) != 0;
    const bool hasUv = (flags & 0x8) != 0;
    const std::uint64_t stride = 12 + (hasFlag2 ? 4 : 0) + (hasFlag4 ? 4 : 0) + (hasUv ? 8 : 0);
    std::uint64_t vertexBytes = 0;
    if (!checkedProduct(count, stride, vertexBytes) || vertexBytes > end - start - 8) return false;
    const auto stripCountOffset64 = std::uint64_t(start) + 8 + vertexBytes;
    if (stripCountOffset64 + 4 > end) return false;
    const auto stripCount = readU32(bytes, static_cast<std::size_t>(stripCountOffset64));
    if (stripCount == 0 || stripCount > options.maxStripsPerDrawable ||
        std::uint64_t(stripCount) * 2 > end - static_cast<std::size_t>(stripCountOffset64) - 4) return false;

    const std::uint64_t payloadBytes = std::uint64_t(count) * sizeof(VkModelVertex) +
                                       std::uint64_t(stripCount) * sizeof(std::uint16_t);
    if (payloadBytes > options.maxDecodedBytes - (std::min)(decodedSoFar, options.maxDecodedBytes))
        return false;

    std::vector<std::uint16_t> stripLengths;
    stripLengths.reserve(stripCount);
    std::uint64_t vertexSum = 0;
    for (std::uint32_t i = 0; i < stripCount; ++i) {
        const auto length = readU16(bytes, static_cast<std::size_t>(stripCountOffset64) + 4 + i * 2);
        if (length < 3 || vertexSum + length > count) return false;
        vertexSum += length;
        stripLengths.push_back(length);
    }
    if (vertexSum != count) return false;

    std::uint64_t decodedBytes = 0;
    if (!checkedProduct(count, sizeof(VkModelVertex), decodedBytes) || decodedBytes > options.maxDecodedBytes - (std::min)(decodedSoFar, options.maxDecodedBytes))
        return false;

    std::vector<VkModelVertex> vertices;
    vertices.reserve(count);
    auto offset = start + 8;
    for (std::uint32_t i = 0; i < count; ++i) {
        if ((i % 256) == 0 && stop.stop_requested()) return false;
        VkModelVertex vertex;
        for (auto& component : vertex.position) {
            component = readFloat(bytes, offset);
            offset += 4;
            if (!std::isfinite(component) || std::abs(component) > 1000000.0f) return false;
        }
        if (hasFlag4) for (auto& component : vertex.attributeFlag4) component = bytes[offset++];
        if (hasFlag2) for (auto& component : vertex.attributeFlag2) component = bytes[offset++];
        if (hasUv) {
            vertex.uv[0] = readFloat(bytes, offset); offset += 4;
            vertex.uv[1] = readFloat(bytes, offset); offset += 4;
            if (!std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1])) return false;
        }
        vertices.push_back(vertex);
    }

    result.objectIndex = record.objectIndex;
    result.serializationVersion = record.version;
    result.vertexFlags = flags;
    result.entryHeaderOffset = record.typeOffset >= 8 ? record.typeOffset - 8 : record.typeOffset;
    result.vertexDataOffset = start + 8;
    result.hasAttributeFlag2 = hasFlag2;
    result.hasAttributeFlag4 = hasFlag4;
    result.hasUv = hasUv;
    result.vertices = std::move(vertices);
    result.stripLengths = std::move(stripLengths);
    return true;
}

std::string versionText(const std::uint32_t version)
{
    return std::to_string((version >> 24) & 0xff) + "." + std::to_string((version >> 16) & 0xff) + "." +
           std::to_string(version & 0xffff);
}
}

bool loadVkModelGeometry(const std::filesystem::path& archivePath, const PckEntry& entry,
                         const VkModelLoadOptions& options, VkModelGeometry& result,
                         std::string& error, std::stop_token stop)
{
    result = {};
    error.clear();
    if (entry.type != "vkModel") {
        error = "Selected PCK member is not a vkModel.";
        return false;
    }
    if ((entry.serializationVersion & 0xff000000) != 0x03000000 ||
        entry.serializationVersion < minFlagStreamVersion) {
        error = "Unsupported vkModel serialization version " + versionText(entry.serializationVersion) +
                "; supported geometry path starts at 3.0.59.";
        return false;
    }
    if (entry.size == 0 || entry.size > options.maxMemberBytes ||
        entry.size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
        error = "vkModel member is empty or exceeds the configured 16-MiB read limit.";
        return false;
    }

    PckArchive archive;
    if (!archive.open(archivePath, error, stop)) return false;
    const auto found = std::find_if(archive.entries().begin(), archive.entries().end(), [&](const PckEntry& candidate) {
        return candidate.offset == entry.offset && candidate.size == entry.size && candidate.name == entry.name && candidate.type == entry.type;
    });
    if (found == archive.entries().end()) {
        error = "vkModel member identity changed since it was selected.";
        return false;
    }
    const auto entryIndex = static_cast<std::size_t>(found - archive.entries().begin());

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(entry.size));
    for (std::uint64_t offset = 0; offset < entry.size; offset += readWindow) {
        if (stop.stop_requested()) { error = "Model loading cancelled."; return false; }
        const auto amount = static_cast<std::size_t>((std::min)(std::uint64_t(readWindow), entry.size - offset));
        if (archive.readEntry(entryIndex, offset, bytes.data() + static_cast<std::size_t>(offset), amount, error) != amount) {
            if (error.empty()) error = "Short bounded read while loading the vkModel member.";
            return false;
        }
    }

    const auto records = findStripRecords(bytes, options.maxDrawables, stop);
    if (stop.stop_requested()) { error = "Model loading cancelled."; return false; }
    if (records.empty()) {
        error = "No supported vkPS2TriStrips records were found, or the drawable record limit was exceeded.";
        return false;
    }
    if (records.size() > options.maxDrawables) {
        error = "vkPS2TriStrips record count exceeds the configured drawable limit.";
        return false;
    }

    std::uint64_t probes = 0;
    std::uint64_t decodedBytes = 0;
    result.archivePath = archivePath.string();
    result.memberName = entry.name;
    result.memberOffset = entry.offset;
    result.memberSize = entry.size;
    result.modelVersion = entry.serializationVersion;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto end = i + 1 < records.size() ? records[i + 1].typeOffset - 8 : bytes.size();
        VkModelDrawable drawable;
        bool decoded = false;
        for (auto cursor = records[i].bodyOffset; cursor + 12 <= end; ++cursor) {
            if (stop.stop_requested()) { result = {}; error = "Model loading cancelled."; return false; }
            if (++probes > options.maxCandidateProbes) {
                result = {}; error = "Model candidate probe budget exceeded."; return false;
            }
            if (parseVertexStream(bytes, cursor, end, records[i], options, decodedBytes, drawable, stop)) {
                decoded = true;
                decodedBytes += drawable.vertices.size() * sizeof(VkModelVertex) + drawable.stripLengths.size() * sizeof(std::uint16_t);
                break;
            }
        }
        if (!decoded) {
            ++result.unsupportedDrawableRecords;
            continue;
        }
        SDL_Log("GameRuntime vkModel: member=%s object=%u version=%s header=0x%llx vertexData=0x%llx vertices=%zu strips=%zu flags=0x%x",
                entry.name.c_str(), drawable.objectIndex, versionText(drawable.serializationVersion).c_str(),
                static_cast<unsigned long long>(entry.offset + drawable.entryHeaderOffset),
                static_cast<unsigned long long>(entry.offset + drawable.vertexDataOffset), drawable.vertices.size(),
                drawable.stripLengths.size(), drawable.vertexFlags);
        result.drawables.push_back(std::move(drawable));
    }
    if (stop.stop_requested()) { result = {}; error = "Model loading cancelled."; return false; }
    if (result.drawables.empty()) {
        error = "Found " + std::to_string(records.size()) + " vkPS2TriStrips record(s), but none matched a bounded "
                "versioned vertex/strip stream. Unsupported records: " + std::to_string(result.unsupportedDrawableRecords) + ".";
        return false;
    }
    SDL_Log("GameRuntime vkModel: member=%s version=%s decodedDrawables=%zu unsupportedDrawables=%u decodedBytes=%llu",
            entry.name.c_str(), versionText(entry.serializationVersion).c_str(), result.drawables.size(),
            result.unsupportedDrawableRecords, static_cast<unsigned long long>(decodedBytes));
    return true;
}
}
