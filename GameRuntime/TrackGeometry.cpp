#include "TrackGeometry.h"

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
class EntryCursor
{
public:
    EntryCursor(const std::filesystem::path& path, const PckEntry& entry)
        : stream_(path, std::ios::binary), base_(entry.offset), size_(entry.size)
    {
    }

    bool good() const { return stream_.good(); }
    std::uint64_t position() const { return position_; }

    bool seek(std::uint64_t offset)
    {
        if (offset > size_ || offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()))
            return fail("seek is outside the indexed member");
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(base_ + offset));
        if (!stream_) return fail("could not seek within the indexed member");
        position_ = offset;
        return true;
    }

    bool read(void* destination, const std::size_t count)
    {
        if (count > size_ - (std::min)(position_, size_)) return fail("truncated member at byte " + std::to_string(position_));
        if (count && !stream_.read(static_cast<char*>(destination), static_cast<std::streamsize>(count)))
            return fail("short read at member byte " + std::to_string(position_));
        position_ += count;
        return true;
    }

    bool skip(std::uint64_t count)
    {
        if (count > size_ - (std::min)(position_, size_)) return fail("stream extent exceeds indexed member at byte " + std::to_string(position_));
        return seek(position_ + count);
    }

    bool u8(std::uint8_t& value) { return read(&value, 1); }
    bool u16(std::uint16_t& value)
    {
        std::array<std::uint8_t, 2> b{};
        if (!read(b.data(), b.size())) return false;
        value = std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8);
        return true;
    }
    bool i16(std::int16_t& value) { std::uint16_t raw; if (!u16(raw)) return false; value = static_cast<std::int16_t>(raw); return true; }
    bool u32(std::uint32_t& value)
    {
        std::array<std::uint8_t, 4> b{};
        if (!read(b.data(), b.size())) return false;
        value = std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
        return true;
    }
    bool i32(std::int32_t& value) { std::uint32_t raw; if (!u32(raw)) return false; value = static_cast<std::int32_t>(raw); return true; }
    bool f32(float& value) { std::uint32_t raw; if (!u32(raw)) return false; value = std::bit_cast<float>(raw); return true; }
    bool string(std::string& value, std::size_t length)
    {
        if (length > 4096) return fail("string length exceeds 4096 bytes");
        value.resize(length);
        if (length && !read(value.data(), length)) return false;
        if (!value.empty() && value.back() == '\0') value.pop_back();
        if (!std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; }))
            return fail("non-ASCII or control character in texture name");
        return true;
    }
    const std::string& error() const { return error_; }

private:
    bool fail(std::string reason) { if (error_.empty()) error_ = std::move(reason); return false; }
    std::ifstream stream_;
    std::uint64_t base_ = 0, size_ = 0, position_ = 0;
    std::string error_;
};

bool checkedProduct(const std::uint64_t a, const std::uint64_t b, std::uint64_t& product)
{
    if (a && b > (std::numeric_limits<std::uint64_t>::max)() / a) return false;
    product = a * b;
    return true;
}

bool boundedCount(const std::uint32_t value, const std::uint32_t maximum, const char* label,
                  std::string& error)
{
    if (value > maximum) { error = std::string(label) + " count exceeds configured limit"; return false; }
    return true;
}
}

bool loadPg1TrackMesh(const std::filesystem::path& archivePath, const PckEntry& entry,
                      const TrackLoadOptions& options, TrackMesh& result, std::string& error)
{
    result = {};
    error.clear();
    if (entry.type != "vkTrack" || entry.name.find("new_provinggrounds.trk") == std::string::npos) {
        error = "Selected member is not the identified PG1 new_provinggrounds track resource.";
        return false;
    }
    if (entry.serializationVersion != 0x03000055) {
        error = "Unsupported vkTrack serialization version; expected observed version 3.0.85.";
        return false;
    }
    if (entry.size < 0x52 || entry.size > 32 * 1024 * 1024) {
        error = "Track member length is outside the supported 82-byte to 32-MiB range.";
        return false;
    }
    EntryCursor input(archivePath, entry);
    if (!input.good()) { error = "Could not open the PCK file for track decoding."; return false; }

    // Exact serialized prefix observed in PG1.PCK at entry-relative +0x4a.
    if (!input.seek(0x4a)) { error = input.error(); return false; }
    std::int32_t segmentCount = 0, bspCount = 0, bspVersion = 0;
    if (!input.i32(segmentCount) || !input.i32(bspCount) || !input.i32(bspVersion)) { error = input.error(); return false; }
    if (segmentCount != 0 || bspCount != 1 || bspVersion != 7) {
        error = "Unsupported PG1 track prefix or BSP version at member byte 0x4a.";
        return false;
    }

    std::uint32_t nodeCount = 0;
    if (!input.u32(nodeCount) || !boundedCount(nodeCount, options.maxNodes, "BSP node", error)) {
        if (error.empty()) error = input.error();
        return false;
    }
    std::uint64_t nodeBytes = 0;
    if (!checkedProduct(nodeCount, 40, nodeBytes) || !input.skip(nodeBytes)) { error = input.error().empty() ? "BSP node extent overflow" : input.error(); return false; }

    std::uint32_t textureCount = 0;
    if (!input.u32(textureCount) || !boundedCount(textureCount, options.maxTextures, "texture", error)) {
        if (error.empty()) error = input.error();
        return false;
    }
    std::vector<std::string> textureNames;
    textureNames.reserve(textureCount);
    for (std::uint32_t i = 0; i < textureCount; ++i) {
        std::int16_t present = 0;
        if (!input.i16(present)) { error = input.error(); return false; }
        std::string name;
        std::int16_t width = 0;
        if (present) {
            std::uint16_t nameLength = 0;
            if (!input.u16(nameLength) || !input.string(name, std::size_t(nameLength) + 1) || !input.i16(width)) { error = input.error(); return false; }
            if (width > 0) {
                std::uint16_t height = 0, channels = 0;
                if (!input.u16(height) || !input.u16(channels)) { error = input.error(); return false; }
                std::uint64_t textureBytes = 0;
                if (!height || !channels || !checkedProduct(std::uint64_t(width) * height, channels, textureBytes) || textureBytes > options.maxDecodedBytes) {
                    error = "Embedded texture dimensions exceed the configured byte limit at member byte " + std::to_string(input.position());
                    return false;
                }
                if (!input.skip(textureBytes)) { error = input.error(); return false; }
            }
        }
        std::int32_t userData = 0, flags = 0;
        if (!input.i32(userData) || !input.i32(flags)) { error = input.error(); return false; }
        textureNames.push_back(std::move(name));
    }

    std::uint32_t leafCount = 0;
    if (!input.u32(leafCount) || !boundedCount(leafCount, options.maxLeaves, "leaf", error)) {
        if (error.empty()) error = input.error();
        return false;
    }
    if (options.leafIndex >= leafCount) { error = "Requested leaf index is outside the parsed BSP."; return false; }

    for (std::uint32_t leaf = 0; leaf <= options.leafIndex; ++leaf) {
        std::uint16_t declaredVertices = 0, batchCount = 0;
        if (!input.u16(declaredVertices) || !input.u16(batchCount)) { error = input.error(); return false; }
        if (batchCount > options.maxBatchesPerLeaf) { error = "Per-leaf batch count exceeds configured limit at member byte " + std::to_string(input.position() - 2); return false; }
        std::uint64_t observedLeafVertices = 0;
        for (std::uint32_t batch = 0; batch < batchCount; ++batch) {
            const auto batchOffset = input.position();
            std::int16_t textureIndex = -1;
            std::int32_t vertexCount = 0, stripCount = 0;
            if (!input.i16(textureIndex) || !input.i32(vertexCount) || !input.i32(stripCount)) { error = input.error(); return false; }
            if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= textureNames.size()) {
                error = "Texture reference is outside the track texture table at member byte " + std::to_string(batchOffset); return false;
            }
            if (vertexCount <= 0 || static_cast<std::uint32_t>(vertexCount) > options.maxVerticesPerBatch || stripCount <= 0 || stripCount > vertexCount) {
                error = "Invalid vertex or strip count at member byte " + std::to_string(batchOffset + 2); return false;
            }
            std::vector<std::uint16_t> strips(static_cast<std::size_t>(stripCount));
            std::uint64_t stripSum = 0;
            for (auto& length : strips) {
                if (!input.u16(length) || length < 3) { error = input.error().empty() ? "Strip length is less than three vertices" : input.error(); return false; }
                stripSum += length;
            }
            if (stripSum != static_cast<std::uint32_t>(vertexCount)) { error = "Strip lengths do not sum to batch vertex count at member byte " + std::to_string(batchOffset); return false; }
            observedLeafVertices += static_cast<std::uint32_t>(vertexCount);
            std::uint64_t vertexBytes = 0;
            if (!checkedProduct(static_cast<std::uint32_t>(vertexCount), 24, vertexBytes)) { error = "Vertex byte count overflow"; return false; }

            const bool selected = leaf == options.leafIndex && batch == options.batchIndex;
            if (!selected) {
                if (!input.skip(vertexBytes)) { error = input.error(); return false; }
                continue;
            }
            if (vertexBytes > options.maxDecodedBytes || vertexBytes + strips.size() * sizeof(std::uint16_t) > options.maxDecodedBytes) {
                error = "Selected mesh exceeds configured decoded-memory limit"; return false;
            }
            result.archivePath = archivePath.string();
            result.memberName = entry.name;
            result.memberOffset = entry.offset;
            result.batchOffset = batchOffset;
            result.vertexDataOffset = input.position();
            result.bspVersion = static_cast<std::uint32_t>(bspVersion);
            result.leafIndex = leaf;
            result.batchIndex = batch;
            result.textureIndex = textureIndex;
            result.textureName = textureNames[static_cast<std::size_t>(textureIndex)];
            result.stripLengths = std::move(strips);
            result.vertices.resize(static_cast<std::size_t>(vertexCount));
            for (auto& vertex : result.vertices) {
                for (auto& component : vertex.position) if (!input.f32(component) || !std::isfinite(component)) { error = input.error().empty() ? "Non-finite position at member byte " + std::to_string(input.position() - 4) : input.error(); result = {}; return false; }
                if (!input.read(vertex.color.data(), vertex.color.size())) { error = input.error(); result = {}; return false; }
                for (auto& component : vertex.uv) if (!input.f32(component) || !std::isfinite(component)) { error = input.error().empty() ? "Non-finite UV at member byte " + std::to_string(input.position() - 4) : input.error(); result = {}; return false; }
            }
        }
        if (observedLeafVertices != declaredVertices) { error = "Leaf vertex total disagrees with its batches at member byte " + std::to_string(input.position()); result = {}; return false; }
        std::uint16_t leafFlags = 0;
        std::int32_t collisionCount = 0;
        if (!input.u16(leafFlags) || !input.i32(collisionCount)) { error = input.error(); result = {}; return false; }
        if (collisionCount < 0 || collisionCount > 1'000'000) { error = "Collision node count exceeds supported bounds at member byte " + std::to_string(input.position() - 4); result = {}; return false; }
        std::uint64_t collisionBytes = 0;
        if (!checkedProduct(static_cast<std::uint32_t>(collisionCount), 16, collisionBytes) || !input.skip(collisionBytes)) {
            error = input.error().empty() ? "Collision node byte extent overflow" : input.error(); result = {}; return false;
        }
    }
    if (result.vertices.empty()) { error = "Requested batch does not exist in the selected leaf."; return false; }
    return true;
}
}
