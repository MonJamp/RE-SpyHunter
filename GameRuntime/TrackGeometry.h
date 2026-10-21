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
    TrackBatchGeometry selected;
};

// Imports one bounded PG1 track and retains only the requested leaf/batch.
// The original positions/UVs/colors are preserved; triangle winding restarts
// independently for each source strip.
bool importTrackBatch(const std::filesystem::path& archivePath,
                      std::string_view loadName,
                      std::uint32_t leaf,
                      std::uint32_t batch,
                      std::stop_token stop,
                      TrackGeometryImport& result,
                      std::string& error);
}
