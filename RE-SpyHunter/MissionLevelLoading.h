#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace spyhunter::game
{
enum class MissionLoadStage { staticArchive, dynamicArchive, companionFile };

struct MissionLoadProgress
{
    MissionLoadStage stage;
    std::string_view item;
    std::uint64_t completedBytes;
    std::uint64_t totalBytes;
};

// Bounded package inspection for the only mission whose load sequence is
// currently supported by target evidence. This verifies and reads the source
// resources; it does not deserialize models or start gameplay.
bool inspectProvingGroundsPackage(const std::filesystem::path& extractedRoot,
                                 const std::function<void(const MissionLoadProgress&)>& progress,
                                 std::string& error);
}
