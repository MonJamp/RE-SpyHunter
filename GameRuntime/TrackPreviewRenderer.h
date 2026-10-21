#pragma once

#include "TrackGeometry.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace spyhunter::runtime
{
namespace detail
{
struct PreviewCameraVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
    std::array<float, 3> color{};
};
}
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

// Deterministic CPU inspection session. Retains depth/transform scratch;
// the caller owns the reusable color target. SDL presents the completed image.
class TrackPreviewRenderer
{
public:
    bool render(const TrackBatchGeometry& mesh, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>& rgba, TrackPreviewStats& stats,
                std::string& error);
    bool render(const TrackBatchGeometry& mesh, const TrackPreviewCamera& camera,
                std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>& rgba, TrackPreviewStats& stats,
                std::string& error);

private:
    // Reused across frames; one renderer session belongs to one caller/thread.
    std::vector<float> depthBuffer_;
    std::vector<detail::PreviewCameraVertex> cameraVertices_;
};
}
