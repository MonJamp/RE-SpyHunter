#pragma once

#include "PckArchive.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
struct TrackVertex
{
    std::array<float, 3> position{};
    std::array<std::uint8_t, 4> color{};
    std::array<float, 2> uv{};
};

struct TrackMesh
{
    std::string archivePath;
    std::string memberName;
    std::uint64_t memberOffset = 0;
    std::uint64_t batchOffset = 0;
    std::uint64_t vertexDataOffset = 0;
    std::uint32_t bspVersion = 0;
    std::uint32_t leafIndex = 0;
    std::uint32_t batchIndex = 0;
    std::int32_t textureIndex = -1;
    std::string textureName;
    std::vector<TrackVertex> vertices;
    std::vector<std::uint16_t> stripLengths;
};

struct TrackLoadOptions
{
    std::uint32_t leafIndex = 74;
    std::uint32_t batchIndex = 5;
    std::uint64_t maxDecodedBytes = 16 * 1024 * 1024;
    std::uint32_t maxNodes = 4096;
    std::uint32_t maxLeaves = 4096;
    std::uint32_t maxTextures = 4096;
    std::uint32_t maxBatchesPerLeaf = 4096;
    std::uint32_t maxVerticesPerBatch = 65535;
};

// Reads the observed PG1.PCK 3.0.85 / track BSP v7 fixture. This deliberately
// does not interpret .vk model resources or claim support for other track versions.
bool loadPg1TrackMesh(const std::filesystem::path& archivePath, const PckEntry& entry,
                      const TrackLoadOptions& options, TrackMesh& result, std::string& error);
}
