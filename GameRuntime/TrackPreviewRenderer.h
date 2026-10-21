#pragma once

#include "TrackGeometry.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
struct TrackPreviewCamera
{
    std::array<float, 3> eye{};
    std::array<float, 3> target{};
    float pitchDegrees = 55.0f;
    float verticalFovDegrees = 60.0f;
    float nearPlane = 1.0f;
    float farPlane = 2'000.0f;
};

struct TrackPreviewStats
{
    std::uint32_t inputTriangles = 0;
    std::uint32_t nearClippedTriangles = 0;
    std::uint32_t farClippedTriangles = 0;
    std::uint32_t rasterizedTriangles = 0;
    std::uint64_t depthPassedPixels = 0;
    TrackPreviewCamera camera;
};

// Deterministic CPU inspection renderer. It owns a color target and depth
// buffer; SDL remains responsible only for presenting the completed image.
class TrackPreviewRenderer
{
public:
    bool render(const TrackBatchGeometry& mesh, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>& rgba, TrackPreviewStats& stats,
                std::string& error) const;
};
}
