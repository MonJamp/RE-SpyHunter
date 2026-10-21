#pragma once

#include <cstdint>
#include <filesystem>
#include "../GameRuntime/TrackGeometry.h"
#include <functional>
#include <string>
#include <string_view>
#include <stop_token>

namespace spyhunter::game
{
enum class MissionLoadStage { staticArchive, staticLevelData, dynamicArchive, levelInitializationData, trackGeometry };

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
                                 std::stop_token stop,
                                 const std::function<bool(const MissionLoadProgress&)>& progress,
                                 std::string& error);
}
