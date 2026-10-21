#pragma once

#include "PckArchive.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <stop_token>
#include <vector>

namespace spyhunter::runtime
{
struct VkModelVertex
{
    std::array<float, 3> position{};
    std::array<std::uint8_t, 4> attributeFlag2{};
    std::array<std::uint8_t, 4> attributeFlag4{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255}; // Viewer-neutral fallback; source flags remain raw below.
    std::array<float, 2> uv{};
};

struct VkModelDrawable
{
    std::uint32_t objectIndex = 0;
    std::uint32_t serializationVersion = 0;
    std::uint32_t vertexFlags = 0;
    std::uint64_t entryHeaderOffset = 0;
    std::uint64_t vertexDataOffset = 0;
    bool hasAttributeFlag2 = false;
    bool hasAttributeFlag4 = false;
    bool hasUv = false;
    std::vector<VkModelVertex> vertices;
    std::vector<std::uint16_t> stripLengths;
};

struct VkModelGeometry
{
    std::string archivePath;
    std::string memberName;
    std::uint64_t memberOffset = 0;
    std::uint64_t memberSize = 0;
    std::uint32_t modelVersion = 0;
    std::vector<VkModelDrawable> drawables;
    std::uint32_t unsupportedDrawableRecords = 0;
};

struct VkModelLoadOptions
{
    std::uint64_t maxMemberBytes = 16 * 1024 * 1024;
    // Retained vertex/strip payload; excludes bounded member input and metadata.
    std::uint64_t maxDecodedBytes = 64 * 1024 * 1024;
    // Candidate start offsets tried, not a wall-clock execution budget.
    std::uint64_t maxCandidateProbes = 16 * 1024 * 1024;
    std::uint32_t maxDrawables = 4096;
    std::uint32_t maxVerticesPerDrawable = 250000;
    std::uint32_t maxStripsPerDrawable = 4096;
};

// Decodes versioned vkPS2TriStrips geometry embedded in one named vkModel PCK
// member. Model hierarchy transforms and shaders remain visible as separate
// unresolved rendering state; they are not applied by this raw-geometry view.
bool loadVkModelGeometry(const std::filesystem::path& archivePath, const PckEntry& entry,
                         const VkModelLoadOptions& options, VkModelGeometry& result,
                         std::string& error, std::stop_token stop = {});
}
