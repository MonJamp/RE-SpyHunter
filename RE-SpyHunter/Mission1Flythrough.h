#pragma once

#include "../GameRuntime/TrackGeometry.h"
#include "../GameRuntime/TrackPreviewRenderer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace spyhunter::game
{
struct Mission1SplinePoint
{
    std::array<float, 3> position{};
    std::array<float, 3> direction{};
    float length = 0.0f;
};

struct Mission1Spline
{
    std::string name;
    std::vector<Mission1SplinePoint> points;
};

struct Mission1CameraPath
{
    std::vector<std::array<float, 2>> markers; // time in seconds, distance in source units
    Mission1Spline spline;
};

struct Mission1IntroObject
{
    std::string model;
    std::array<float, 3> position{};
    std::array<float, 3> direction{};
};

struct Mission1Flythrough
{
    Mission1CameraPath eye;
    Mission1CameraPath target;
    std::vector<Mission1IntroObject> temporaryObjects;
    std::uint64_t sourceBytes = 0;
};

// Reads the evidenced USA Mission 1 PG1.AI v7 layout (bounded to 128 KiB),
// validates its resource end and builds the source-backed temporary intro set.
bool loadMission1Flythrough(const std::filesystem::path& aiPath,
                            Mission1Flythrough& result, std::string& error);

// Evaluates the independently timed original eye and target paths. Host delta
// capping, tour phase/skip behavior and diagnostic presentation remain with the
// caller; this function only evaluates the recovered spline contract.
bool evaluateMission1Flythrough(const Mission1Flythrough& tour, float elapsedSeconds,
                                runtime::TrackPreviewCamera& camera, std::string& error);

// Append diagnostic proxy meshes at the source positions of the 48 intro
// clones. Dimensions/colors and tangent orientation are explicit host stand-ins
// until the original vkModel hierarchy and placement virtual are implemented.
bool appendMission1IntroProxies(const Mission1Flythrough& tour,
                               runtime::TrackBatchGeometry& scene, std::string& error);
}
