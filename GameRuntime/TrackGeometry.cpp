#include "TrackGeometry.h"
#include "MeshTopology.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace spyhunter::runtime
{
namespace
{
constexpr std::size_t windowSize = 64 * 1024;
constexpr std::uint64_t maximumEntryBytes = 16 * 1024 * 1024;
constexpr std::uint32_t maximumNodes = 100'000;
constexpr std::uint32_t maximumTextures = 4'096;
constexpr std::uint32_t maximumLeaves = 100'000;
constexpr std::uint32_t maximumBatchesPerLeaf = 4'096;
constexpr std::uint32_t maximumVerticesPerBatch = 65'535;
constexpr std::uint32_t maximumStripsPerBatch = 65'535;
constexpr std::uint32_t maximumEmbeddedTextureBytes = 16 * 1024 * 1024;
constexpr std::uint32_t maximumStringBytes = 4'096;

class EntryCursor
{
public:
    EntryCursor(const PckArchive& archive, const std::size_t index, const std::stop_token stop)
        : archive_(archive), index_(index), stop_(stop) {}

    std::uint64_t position() const { return position_; }

    bool read(void* destination, const std::size_t count, std::string& error)
    {
        if (stop_.stop_requested()) { error = "Mission track import cancelled."; return false; }
        if (count > archive_.entries()[index_].size - position_) {
            error = "Track member ends before the required field at offset " + std::to_string(position_) + ".";
            return false;
        }
        auto* output = static_cast<std::uint8_t*>(destination);
        std::size_t remaining = count;
        while (remaining) {
            if (windowOffset_ == windowCount_) {
                windowOffset_ = 0;
                windowCount_ = static_cast<std::size_t>((std::min)(
                    std::uint64_t(window_.size()), archive_.entries()[index_].size - position_));
                if (!windowCount_ || archive_.readEntry(index_, position_, window_.data(), windowCount_, error) != windowCount_) {
                    if (error.empty()) error = "Short bounded track member read at offset " + std::to_string(position_) + ".";
                    return false;
                }
            }
            const auto amount = (std::min)(remaining, windowCount_ - windowOffset_);
            std::memcpy(output, window_.data() + windowOffset_, amount);
            output += amount;
            remaining -= amount;
            windowOffset_ += amount;
            position_ += amount;
        }
        return true;
    }

    bool skip(std::uint64_t count, std::string& error)
    {
        std::array<std::uint8_t, windowSize> discard{};
        while (count) {
            const auto amount = static_cast<std::size_t>((std::min)(count, std::uint64_t(discard.size())));
            if (!read(discard.data(), amount, error)) return false;
            count -= amount;
        }
        return true;
    }

    bool i32(std::int32_t& value, std::string& error)
    {
        std::array<std::uint8_t, 4> bytes{};
        if (!read(bytes.data(), bytes.size(), error)) return false;
        const std::uint32_t bits = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
            (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        value = std::bit_cast<std::int32_t>(bits);
        return true;
    }

    bool u16(std::uint16_t& value, std::string& error)
    {
        std::array<std::uint8_t, 2> bytes{};
        if (!read(bytes.data(), bytes.size(), error)) return false;
        value = std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8);
        return true;
    }

    bool i16(std::int16_t& value, std::string& error)
    {
        std::uint16_t bits = 0;
        if (!u16(bits, error)) return false;
        value = std::bit_cast<std::int16_t>(bits);
        return true;
    }

    bool f32(float& value, std::string& error)
    {
        std::array<std::uint8_t, 4> bytes{};
        if (!read(bytes.data(), bytes.size(), error)) return false;
        const std::uint32_t bits = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
            (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        value = std::bit_cast<float>(bits);
        return true;
    }

    bool string(std::string& value, const std::uint32_t maximum, std::string& error)
    {
        std::int32_t length = 0;
        if (!i32(length, error)) return false;
        if (length < 1 || static_cast<std::uint32_t>(length) > maximum) {
            error = "Invalid track string length at offset " + std::to_string(position_ - 4) + ".";
            return false;
        }
        value.resize(static_cast<std::size_t>(length));
        if (!read(value.data(), value.size(), error)) return false;
        if (value.back() != '\0') {
            error = "Track string is not NUL terminated at offset " + std::to_string(position_ - value.size()) + ".";
            return false;
        }
        value.pop_back();
        return true;
    }

private:
    const PckArchive& archive_;
    std::size_t index_;
    std::stop_token stop_;
    std::uint64_t position_ = 0;
    std::array<std::uint8_t, windowSize> window_{};
    std::size_t windowOffset_ = 0;
    std::size_t windowCount_ = 0;
};

bool readCount(EntryCursor& input, const std::uint32_t maximum, std::uint32_t& value,
               const char* label, std::string& error)
{
    std::int32_t raw = 0;
    if (!input.i32(raw, error)) return false;
    if (raw < 0 || static_cast<std::uint32_t>(raw) > maximum) {
        error = std::string("Unsupported ") + label + " count at track offset " +
            std::to_string(input.position() - 4) + ".";
        return false;
    }
    value = static_cast<std::uint32_t>(raw);
    return true;
}

bool skipArray(EntryCursor& input, const std::uint64_t count, const std::uint64_t elementBytes,
               std::string& error)
{
    if (elementBytes && count > std::numeric_limits<std::uint64_t>::max() / elementBytes) {
        error = "Track array size overflow.";
        return false;
    }
    return input.skip(count * elementBytes, error);
}
}

static bool importTrackBatchImpl(const std::filesystem::path& archivePath, const std::string_view loadName,
                      const std::uint32_t selectedLeaf, const std::uint32_t selectedBatch,
                      const std::stop_token stop,
                      TrackGeometryImport& result, std::string& error, const TrackLoadOptions& limits)
{
    result = {};
    error.clear();
    PckArchive archive;
    if (!archive.open(archivePath, error, stop)) {
        error = archivePath.string() + ": " + error;
        return false;
    }
    std::size_t entryIndex = archive.entries().size();
    for (std::size_t i = 0; i < archive.entries().size(); ++i) {
        const auto& candidate = archive.entries()[i];
        if (candidate.loadName == loadName || (candidate.loadName.size() >= loadName.size() &&
            candidate.loadName.compare(candidate.loadName.size() - loadName.size(), loadName.size(), loadName) == 0)) {
            if (entryIndex != archive.entries().size()) {
                error = "Track load name is ambiguous in " + archivePath.string() + ": " + std::string(loadName);
                return false;
            }
            entryIndex = i;
        }
    }
    if (entryIndex == archive.entries().size()) {
        error = "Track resource is missing from " + archivePath.string() + ": " + std::string(loadName);
        return false;
    }
    const auto& entry = archive.entries()[entryIndex];
    if (entry.size > maximumEntryBytes) {
        error = "Track resource exceeds the 16 MiB import safety cap: " + entry.loadName;
        return false;
    }
    if (entry.type != "vkTrack") {
        error = "Unsupported track resource type in " + archivePath.string() + " [" + entry.loadName + "]: " + entry.type;
        return false;
    }

    EntryCursor input(archive, entryIndex, stop);
    std::array<std::uint8_t, 4> magic{};
    if (!input.read(magic.data(), magic.size(), error) || magic != std::array<std::uint8_t, 4>{0x65,0x32,0x77,0xba}) {
        error = archivePath.string() + " [" + entry.loadName + "] offset 0: invalid serialized object header.";
        return false;
    }
    // Validate the release's general object and plugin header before consuming
    // the vkTrack-specific base fields. PckArchive has already validated the
    // same structure while indexing; rereading prevents blind fixed-offset use.
    std::int32_t header = 0, count = 0;
    std::string plugins;
    if (!input.i32(header, error) || (header & 0xff000000) != 0x03000000 ||
        !input.i32(count, error) || count != 4 || !input.string(plugins, 32, error) || plugins != "vkPlugIns" ||
        !input.i32(header, error) || (header & 0xff000000) != 0x03000000 ||
        !input.i32(count, error) || count < 0 || count > 32) {
        if (error.empty()) error = "Unsupported outer track serialization header.";
        return false;
    }
    for (int i = 0; i < count; ++i) {
        std::string field;
        for (int j = 0; j < 4; ++j) if (!input.string(field, 256, error)) return false;
        std::int32_t ignored = 0;
        if (!input.i32(ignored, error) || !input.i32(ignored, error) || ignored != 0 || !input.i32(ignored, error)) {
            if (error.empty()) error = "Malformed track plugin record.";
            return false;
        }
    }
    std::string type;
    if (!input.string(type, 128, error) || type != "vkTrack" || !input.i32(header, error) ||
        (header & 0xff000000) != 0x03000000) {
        if (error.empty()) error = "Unsupported vkTrack base serialization.";
        return false;
    }
    // The variable base fields are currently only understood through the
    // exact PG1 track prefix. Validate the marker at the evidence-backed
    // boundary instead of assuming every PCK record shares it.
    constexpr std::uint64_t bspPrefix = 0x4a;
    if (input.position() > bspPrefix || !input.skip(bspPrefix - input.position(), error)) return false;
    std::int32_t zero = 0, one = 0, version = 0;
    if (!input.i32(zero, error) || zero != 0 || !input.i32(one, error) || one != 1 ||
        !input.i32(version, error) || version != 7) {
        if (error.empty()) error = "Unsupported PG1 track base fields or BSP version.";
        return false;
    }
    result.archivePath = archivePath.string();
    result.memberName = entry.loadName;
    result.memberOffset = entry.offset;
    result.bspVersion = static_cast<std::uint32_t>(version);

    if (!readCount(input, (std::min)(limits.maxNodes, maximumNodes), result.nodes, "BSP node", error) || !skipArray(input, result.nodes, 40, error)) return false;
    std::uint32_t textureCount = 0;
    if (!readCount(input, (std::min)(limits.maxTextures, maximumTextures), textureCount, "local texture", error)) return false;
    std::vector<std::string> textureNames;
    textureNames.reserve(textureCount);
    for (std::uint32_t texture = 0; texture < textureCount; ++texture) {
        std::int16_t present = 0;
        if (!input.i16(present, error)) return false;
        std::string name;
        if (present) {
            std::uint16_t lengthMinusOne = 0;
            std::int16_t width = 0;
            if (!input.u16(lengthMinusOne, error)) return false;
            // string fields here use the source's uint16 length + raw bytes,
            // not the generic signed-32-bit string helper.
            if (lengthMinusOne + 1 > maximumStringBytes) { error = "Track texture name exceeds the safety cap."; return false; }
            name.resize(lengthMinusOne + 1);
            if (!input.read(name.data(), name.size(), error) || name.back() != '\0' || !input.i16(width, error)) return false;
            name.pop_back();
            if (width > 0) {
                std::uint16_t height = 0, channels = 0;
                if (!input.u16(height, error) || !input.u16(channels, error)) return false;
                const std::uint64_t bytes = std::uint64_t(static_cast<std::uint16_t>(width)) * height * channels;
                if (bytes > maximumEmbeddedTextureBytes || !input.skip(bytes, error)) {
                    if (error.empty()) error = "Embedded track texture exceeds the safety cap.";
                    return false;
                }
            }
        }
        std::int32_t ignored = 0;
        if (version >= 2 && !input.i32(ignored, error)) return false;
        if (version >= 4 && !input.i32(ignored, error)) return false;
        textureNames.push_back(std::move(name));
    }

    if (!readCount(input, (std::min)(limits.maxLeaves, maximumLeaves), result.leaves, "BSP leaf", error) || selectedLeaf >= result.leaves) {
        if (error.empty()) error = "Requested track leaf is outside the imported BSP.";
        return false;
    }
    bool selectedFound = false;
    for (std::uint32_t leaf = 0; leaf < result.leaves; ++leaf) {
        std::uint16_t leafVertexCount = 0, batchCount = 0;
        if (!input.u16(leafVertexCount, error) || !input.u16(batchCount, error) || batchCount > (std::min)(limits.maxBatchesPerLeaf, maximumBatchesPerLeaf)) {
            if (error.empty()) error = "Unsupported BSP leaf header at leaf " + std::to_string(leaf) + ".";
            return false;
        }
        std::uint64_t verticesInLeaf = 0;
        result.batches += batchCount;
        for (std::uint32_t batch = 0; batch < batchCount; ++batch) {
            const auto batchOffset = input.position();
            std::int16_t texture = 0;
            std::uint32_t vertexCount = 0, stripCount = 0;
            if (!input.i16(texture, error) || texture < 0 || static_cast<std::uint32_t>(texture) >= textureCount ||
                !readCount(input, (std::min)(limits.maxVerticesPerBatch, maximumVerticesPerBatch), vertexCount, "batch vertex", error) ||
                !readCount(input, maximumStripsPerBatch, stripCount, "triangle strip", error) || !vertexCount || !stripCount) {
                if (error.empty()) error = "Malformed track batch at leaf " + std::to_string(leaf) + ".";
                return false;
            }
            const bool retain = leaf == selectedLeaf && batch == selectedBatch;
            TrackBatchGeometry candidate;
            if (retain) {
                candidate.leaf = leaf;
                candidate.batch = batch;
                candidate.texture = texture;
                candidate.textureName = textureNames[static_cast<std::size_t>(texture)];
                candidate.batchOffset = batchOffset;
                const std::uint64_t vertexBytes = std::uint64_t(vertexCount) * sizeof(TrackVertex);
                const std::uint64_t stripBytes = std::uint64_t(stripCount) * sizeof(std::uint16_t);
                // Every strip has at least three vertices; retain all source slots.
                if (stripCount > vertexCount / 3) {
                    error = "Track strip count cannot fit the declared vertices.";
                    return false;
                }
                const std::uint64_t triangleCount = vertexCount - std::uint64_t(stripCount) * 2;
                const auto decodedBytes = vertexBytes + stripBytes + triangleCount * sizeof(TrackTriangle);
                if (decodedBytes > limits.maxDecodedBytes) {
                    error = "Selected track geometry exceeds the decoded-memory limit.";
                    return false;
                }
                candidate.triangles.reserve(static_cast<std::size_t>(triangleCount));
                candidate.vertices.reserve(vertexCount);
                candidate.stripLengths.reserve(stripCount);
            }
            std::uint64_t stripVertices = 0;
            for (std::uint32_t strip = 0; strip < stripCount; ++strip) {
                std::uint16_t length = 0;
                if (!input.u16(length, error) || length < 3) {
                    if (error.empty()) error = "Invalid short strip at leaf " + std::to_string(leaf) + ", batch " + std::to_string(batch) + ".";
                    return false;
                }
                stripVertices += length;
                result.triangleSlots += length - 2;
                result.strips += 1;
                if (retain) candidate.stripLengths.push_back(length);
            }
            if (stripVertices != vertexCount) {
                error = "Strip lengths do not sum to batch vertex count at leaf " + std::to_string(leaf) + ", batch " + std::to_string(batch) + ".";
                return false;
            }
            if (retain) candidate.vertexDataOffset = input.position();
            verticesInLeaf += vertexCount;
            result.vertices += vertexCount;
            for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
                TrackVertex value;
                for (auto& coordinate : value.position) if (!input.f32(coordinate, error)) return false;
                if (!input.read(value.color.data(), value.color.size(), error)) return false;
                for (auto& coordinate : value.uv) if (!input.f32(coordinate, error)) return false;
                if (!std::all_of(value.position.begin(), value.position.end(), [](float v) { return std::isfinite(v); }) ||
                    !std::all_of(value.uv.begin(), value.uv.end(), [](float v) { return std::isfinite(v); })) {
                    error = "Non-finite track vertex at leaf " + std::to_string(leaf) + ", batch " + std::to_string(batch) + ".";
                    return false;
                }
                if (retain) candidate.vertices.push_back(value);
            }
            if (retain) {
                forEachStripTriangle(candidate.stripLengths, [&](std::size_t, const auto& indices) {
                    candidate.triangles.push_back({indices});
                });
                result.selected = std::move(candidate);
                selectedFound = true;
            }
        }
        if (verticesInLeaf != leafVertexCount) {
            error = "Leaf vertex count does not match its batches at leaf " + std::to_string(leaf) + ".";
            return false;
        }
        std::uint16_t flags = 0;
        std::uint32_t collisionCount = 0;
        if (!input.u16(flags, error) || !readCount(input, 1'000'000, collisionCount, "collision node", error) ||
            !skipArray(input, collisionCount, 16, error)) return false;
    }
    if (!selectedFound) {
        error = "Requested track batch is missing: leaf " + std::to_string(selectedLeaf) + ", batch " + std::to_string(selectedBatch) + ".";
        return false;
    }
    std::int32_t trailer = 0, segmentTrailer = 0, nextNameLength = 0;
    if (!input.i32(trailer, error) || !input.i32(segmentTrailer, error) || trailer != 0 || segmentTrailer != 0 ||
        !input.i32(nextNameLength, error) || nextNameLength < 1 || nextNameLength > maximumStringBytes) {
        if (error.empty()) error = "Invalid track end markers or following load-name field.";
        return false;
    }
    std::string nextName(static_cast<std::size_t>(nextNameLength), '\0');
    if (!input.read(nextName.data(), nextName.size(), error) || nextName.back() != '\0') return false;
    if (entryIndex + 1 >= archive.entries().size() || archive.entries()[entryIndex + 1].offset != entry.offset + input.position()) {
        error = "Track parsed end does not meet the next indexed PCK resource boundary (archive offset " +
            std::to_string(entry.offset + input.position()) + ").";
        return false;
    }
    result.parsedArchiveOffset = entry.offset + input.position() - static_cast<std::uint64_t>(nextNameLength) - 4;
    return true;
}

bool importTrackBatch(const std::filesystem::path& archivePath, std::string_view loadName,
                      std::uint32_t leaf, std::uint32_t batch, std::stop_token stop,
                      TrackGeometryImport& result, std::string& error,
                      const TrackLoadOptions& limits)
{
    result = {};
    TrackGeometryImport candidate;
    if (!importTrackBatchImpl(archivePath, loadName, leaf, batch, stop, candidate, error, limits))
        return false;
    result = std::move(candidate);
    return true;
}
bool loadPg1TrackMesh(const std::filesystem::path& archivePath, const PckEntry& entry,
                      const TrackLoadOptions& options, TrackMesh& result, std::string& error)
{
    result = {};
    TrackGeometryImport imported;
    if (entry.type != "vkTrack" || entry.serializationVersion != 0x03000055) {
        error = "Unsupported track member; SH Viewer expects the observed PG1 vkTrack 3.0.85 fixture.";
        return false;
    }
    if (options.maxNodes < 1 || options.maxNodes > maximumNodes ||
        options.maxLeaves < 1 || options.maxLeaves > maximumLeaves ||
        options.maxTextures < 1 || options.maxTextures > maximumTextures ||
        options.maxBatchesPerLeaf < 1 || options.maxBatchesPerLeaf > maximumBatchesPerLeaf ||
        options.maxVerticesPerBatch < 1 || options.maxVerticesPerBatch > maximumVerticesPerBatch) {
        error = "SH Viewer track limits are outside the track reader's supported safety bounds.";
        return false;
    }
    if (!importTrackBatch(archivePath, entry.loadName, options.leafIndex, options.batchIndex, {}, imported, error, options)) return false;
    if (imported.selected.vertices.size() * sizeof(TrackVertex) > options.maxDecodedBytes) {
        error = "Selected mesh exceeds SH Viewer configured decoded-memory limit.";
        return false;
    }
    result.archivePath = imported.archivePath;
    result.memberName = imported.memberName;
    result.memberOffset = imported.memberOffset;
    result.bspVersion = imported.bspVersion;
    result.leafIndex = imported.selected.leaf;
    result.batchIndex = imported.selected.batch;
    result.textureIndex = imported.selected.texture;
    result.textureName = imported.selected.textureName;
    result.batchOffset = imported.selected.batchOffset;
    result.vertexDataOffset = imported.selected.vertexDataOffset;
    result.vertices = std::move(imported.selected.vertices);
    result.stripLengths = std::move(imported.selected.stripLengths);
    return true;
}
}
