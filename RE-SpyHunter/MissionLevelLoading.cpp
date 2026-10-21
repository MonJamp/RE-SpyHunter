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

constexpr std::array<Companion, 3> initializationCompanions{{
    {L"PG1.AI"}, {L"OBJECTS.TXT"}, {L"PFX.TXT"}
}};

bool hasLoadNameSuffix(const spyhunter::runtime::PckArchive& archive, const std::string_view suffix)
{
    return std::any_of(archive.entries().begin(), archive.entries().end(), [&](const auto& entry) {
        return entry.loadName.size() >= suffix.size() &&
            entry.loadName.compare(entry.loadName.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
}

bool readArchive(const std::filesystem::path& path, const MissionLoadStage stage,
                 std::stop_token stop,
                 const std::function<bool(const MissionLoadProgress&)>& progress,
                 const std::uint64_t aggregateBase, const std::uint64_t aggregateTotal,
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
            if (stop.stop_requested()) { error = "Mission package inspection cancelled."; return false; }
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
            if (progress && !progress({stage, entry.loadName, aggregateBase + completed, aggregateTotal})) {
                error = "Mission package inspection cancelled.";
                return false;
            }
        }
    }
    return true;
}

bool readCompanion(const std::filesystem::path& path,
                   const MissionLoadStage stage,
                   std::stop_token stop,
                   const std::function<bool(const MissionLoadProgress&)>& progress,
                   const std::uint64_t aggregateBase, const std::uint64_t aggregateTotal,
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
        if (stop.stop_requested()) { error = "Mission package inspection cancelled."; return false; }
        const auto amount = static_cast<std::streamsize>((std::min)(
            std::uint64_t(buffer.size()), total - completed));
        if (!file.read(buffer.data(), amount)) {
            error = "Mission file read ended early: " + path.string();
            return false;
        }
        completed += static_cast<std::uint64_t>(amount);
        if (progress && !progress({stage, path.filename().string(), aggregateBase + completed, aggregateTotal})) {
            error = "Mission package inspection cancelled.";
            return false;
        }
    }
    return true;
}
}

bool inspectProvingGroundsPackage(const std::filesystem::path& extractedRoot,
                                 const std::stop_token stop,
                                 const std::function<bool(const MissionLoadProgress&)>& progress,
                                 std::string& error)
{
    error.clear();
    if (stop.stop_requested()) { error = "Mission package inspection cancelled."; return false; }
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

    std::uint64_t aggregateTotal = 0;
    for (const auto& entry : staticArchive.entries()) aggregateTotal += entry.size;
    for (const auto& entry : dynamicArchive.entries()) aggregateTotal += entry.size;
    {
        std::error_code sizeError;
        aggregateTotal += std::filesystem::file_size(missionRoot / L"PG1.WTR", sizeError);
        if (sizeError) { error = "Could not size PG1.WTR"; return false; }
    }
    for (const auto& companion : initializationCompanions) {
        std::error_code sizeError;
        aggregateTotal += std::filesystem::file_size(missionRoot / companion.path, sizeError);
        if (sizeError) { error = "Could not size required mission file: " + (missionRoot / companion.path).string(); return false; }
    }

    // ShLevel::load reads the loose water file between the static and dynamic
    // archives. ShLevel::init then reads AI, object definitions and effect counts.
    std::uint64_t aggregateCompleted = 0;
    const auto staticProgress = [&](const MissionLoadProgress& update) {
        return !progress || progress({update.stage, update.item,
                                      aggregateCompleted + update.completedBytes, aggregateTotal});
    };
    if (!readArchive(staticArchivePath, MissionLoadStage::staticArchive, stop, staticProgress,
                     0, aggregateTotal, error)) return false;
    std::uint64_t staticSize = 0;
    for (const auto& entry : staticArchive.entries()) staticSize += entry.size;
    aggregateCompleted += staticSize;
    const auto waterProgress = [&](const MissionLoadProgress& update) {
        return !progress || progress({update.stage, update.item, aggregateCompleted + update.completedBytes,
                                      aggregateTotal});
    };
    if (!readCompanion(missionRoot / L"PG1.WTR", MissionLoadStage::staticLevelData, stop, waterProgress,
                       0, aggregateTotal, error)) return false;
    std::error_code waterSizeError;
    aggregateCompleted += std::filesystem::file_size(missionRoot / L"PG1.WTR", waterSizeError);
    if (waterSizeError) { error = "Could not size PG1.WTR"; return false; }
    const auto dynamicProgress = [&](const MissionLoadProgress& update) {
        return !progress || progress({update.stage, update.item,
                                      aggregateCompleted + update.completedBytes, aggregateTotal});
    };
    if (!readArchive(dynamicArchivePath, MissionLoadStage::dynamicArchive, stop, dynamicProgress,
                     0, aggregateTotal, error)) return false;
    aggregateCompleted += [&] { std::uint64_t result = 0; for (const auto& entry : dynamicArchive.entries()) result += entry.size; return result; }();

    for (const auto& companion : initializationCompanions) {
        const auto path = missionRoot / companion.path;
        if (!readCompanion(path, MissionLoadStage::levelInitializationData, stop, progress,
                           aggregateCompleted, aggregateTotal, error)) return false;
        std::error_code companionSizeError;
        aggregateCompleted += std::filesystem::file_size(path, companionSizeError);
        if (companionSizeError) { error = "Could not size required mission file: " + path.string(); return false; }
    }
    return true;
}
}
