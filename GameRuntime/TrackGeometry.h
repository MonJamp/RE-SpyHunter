#pragma once

#include "PckArchive.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <stop_token>
#include <vector>

namespace spyhunter::runtime
{
struct TrackVertex
{
    std::array<float, 3> position{};
    std::array<std::uint8_t, 4> color{};
    std::array<float, 2> uv{};
};

struct TrackTriangle
{
    std::array<std::uint32_t, 3> vertices{};
};

struct TrackBatchGeometry
{
    std::uint32_t leaf = 0;
    std::uint32_t batch = 0;
    std::int16_t texture = -1;
    std::string textureName;
    std::uint64_t batchOffset = 0;
    std::uint64_t vertexDataOffset = 0;
    std::vector<TrackVertex> vertices;
    std::vector<std::uint16_t> stripLengths;
    std::vector<TrackTriangle> triangles;
};

struct TrackGeometryImport
{
    std::string archivePath;
    std::string memberName;
    std::uint32_t bspVersion = 0;
    std::uint32_t nodes = 0;
    std::uint32_t leaves = 0;
    std::uint32_t batches = 0;
    std::uint64_t vertices = 0;
    std::uint64_t strips = 0;
    std::uint64_t triangleSlots = 0;
    std::uint64_t parsedArchiveOffset = 0;
    std::uint64_t memberOffset = 0;
    TrackBatchGeometry selected;
};

// Compatibility view consumed by SH Viewer. Data is still sourced through
// the bounded, cancellable importer above.
struct TrackMesh
{
    std::string archivePath, memberName, textureName;
    std::uint64_t memberOffset = 0, batchOffset = 0, vertexDataOffset = 0;
    std::uint32_t bspVersion = 0, leafIndex = 0, batchIndex = 0;
    std::int32_t textureIndex = -1;
    std::vector<TrackVertex> vertices;
    std::vector<std::uint16_t> stripLengths;
};
struct TrackLoadOptions
{
    std::uint32_t leafIndex = 74, batchIndex = 5;
    // Selected payload: vertices + strips + prepared triangle slots. Archive
    // index, texture-name metadata and allocator overhead are separate bounds.
    std::uint64_t maxDecodedBytes = 16 * 1024 * 1024;
    std::uint32_t maxNodes = 100'000, maxLeaves = 100'000, maxTextures = 4'096;
    std::uint32_t maxBatchesPerLeaf = 4'096, maxVerticesPerBatch = 65'535;
};
bool loadPg1TrackMesh(const std::filesystem::path& archivePath, const PckEntry& entry,
                      const TrackLoadOptions& options, TrackMesh& result, std::string& error);

// Imports one bounded PG1 track and retains only the requested leaf/batch.
// The original positions/UVs/colors are preserved; triangle winding restarts
// independently for each source strip.
bool importTrackBatch(const std::filesystem::path& archivePath,
                      std::string_view loadName,
                      std::uint32_t leaf,
                      std::uint32_t batch,
                      std::stop_token stop,
                      TrackGeometryImport& result,
                      std::string& error,
                      const TrackLoadOptions& limits = {});
}
