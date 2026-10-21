#include "MissionLevelLoading.h"

#include "../GameRuntime/PckArchive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <vector>

namespace spyhunter::game
{
namespace
{
constexpr std::size_t readChunkBytes = 64 * 1024;

struct Companion
{
    const wchar_t* path;
};

constexpr std::array<Companion, 4> companions{{
    {L"PG1.AI"}, {L"PG1.WTR"}, {L"OBJECTS.TXT"}, {L"PFX.TXT"}
}};

bool hasLoadNameSuffix(const spyhunter::runtime::PckArchive& archive, const std::string_view suffix)
{
    return std::any_of(archive.entries().begin(), archive.entries().end(), [&](const auto& entry) {
        return entry.loadName.size() >= suffix.size() &&
            entry.loadName.compare(entry.loadName.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
}

bool readArchive(const std::filesystem::path& path, const MissionLoadStage stage,
                 const std::function<void(const MissionLoadProgress&)>& progress,
                 std::string& error)
{
    spyhunter::runtime::PckArchive archive;
    if (!archive.open(path, error)) {
        error = path.string() + ": " + error;
        return false;
    }

    std::uint64_t total = 0;
    for (const auto& entry : archive.entries()) total += entry.size;
    std::array<char, readChunkBytes> buffer{};
    std::uint64_t completed = 0;
    for (std::size_t index = 0; index < archive.entries().size(); ++index) {
        const auto& entry = archive.entries()[index];
        std::uint64_t offset = 0;
        while (offset < entry.size) {
            const auto amount = static_cast<std::size_t>((std::min)(
                std::uint64_t(buffer.size()), entry.size - offset));
            const auto received = archive.readEntry(index, offset, buffer.data(), amount, error);
            if (received != amount) {
                error = path.string() + " [" + entry.loadName + "]: " +
                    (error.empty() ? "short archive member read." : error);
                return false;
            }
            offset += received;
            completed += received;
            if (progress) progress({stage, entry.loadName, completed, total});
        }
    }
    return true;
}

bool readCompanion(const std::filesystem::path& path,
                   const std::function<void(const MissionLoadProgress&)>& progress,
                   std::string& error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Required mission file is missing or unreadable: " + path.string();
        return false;
    }
    const auto end = file.tellg();
    if (end < 0) {
        error = "Could not determine mission file size: " + path.string();
        return false;
    }
    const auto total = static_cast<std::uint64_t>(end);
    file.seekg(0);
    std::array<char, readChunkBytes> buffer{};
    std::uint64_t completed = 0;
    while (completed < total) {
        const auto amount = static_cast<std::streamsize>((std::min)(
            std::uint64_t(buffer.size()), total - completed));
        if (!file.read(buffer.data(), amount)) {
            error = "Mission file read ended early: " + path.string();
            return false;
        }
        completed += static_cast<std::uint64_t>(amount);
        if (progress) progress({MissionLoadStage::companionFile, path.filename().string(), completed, total});
    }
    return true;
}
}

bool inspectProvingGroundsPackage(const std::filesystem::path& extractedRoot,
                                 const std::function<void(const MissionLoadProgress&)>& progress,
                                 std::string& error)
{
    error.clear();
    const auto missionRoot = extractedRoot / "PG1";
    const auto staticArchivePath = missionRoot / "PG1.PCK";
    const auto dynamicArchivePath = missionRoot / "PG1D.PCK";

    spyhunter::runtime::PckArchive staticArchive;
    if (!staticArchive.open(staticArchivePath, error)) {
        error = staticArchivePath.string() + ": " + error;
        return false;
    }
    constexpr std::array<std::string_view, 3> requiredStatic{
        "new_provinggrounds.trk", "new_provinggrounds.wtr", "overcast_dawn.vk"
    };
    for (const auto name : requiredStatic) {
        if (!hasLoadNameSuffix(staticArchive, name)) {
            error = "Mission 1 static archive is missing " + std::string(name);
            return false;
        }
    }

    spyhunter::runtime::PckArchive dynamicArchive;
    if (!dynamicArchive.open(dynamicArchivePath, error)) {
        error = dynamicArchivePath.string() + ": " + error;
        return false;
    }
    constexpr std::array<std::string_view, 8> requiredExternalModels{
        "oval_ext.vk", "land_city_ext.vk", "water_city_ext.vk", "water_slolam_ext.vk",
        "water_target_ext.vk", "water_to_oval_ext.vk", "winding_road_ext.vk", "highway_ext.vk"
    };
    for (const auto name : requiredExternalModels) {
        if (!hasLoadNameSuffix(dynamicArchive, name)) {
            error = "Mission 1 dynamic archive is missing " + std::string(name);
            return false;
        }
    }

    // Follow ShLoadLevelState's package order. Import/decoding remains a
    // separate boundary; this pass performs bounded reads to verify the data.
    if (!readArchive(staticArchivePath, MissionLoadStage::staticArchive, progress, error) ||
        !readArchive(dynamicArchivePath, MissionLoadStage::dynamicArchive, progress, error)) return false;

    for (const auto& companion : companions) {
        const auto path = missionRoot / companion.path;
        if (!readCompanion(path, progress, error)) return false;
    }
    return true;
}
}
