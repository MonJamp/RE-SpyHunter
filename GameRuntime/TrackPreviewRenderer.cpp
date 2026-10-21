#include "TrackPreviewRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace spyhunter::runtime
{
namespace
{
constexpr std::uint64_t maximumPixels = 1280ull * 896ull;
constexpr std::uint64_t maximumImageBytes = maximumPixels * 4;

struct CameraVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
    std::array<float, 3> color{};
};

struct ScreenVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
    std::array<float, 3> color{};
};

CameraVertex interpolate(const CameraVertex& a, const CameraVertex& b, const float t)
{
    CameraVertex result;
    result.x = a.x + (b.x - a.x) * t;
    result.y = a.y + (b.y - a.y) * t;
    result.depth = a.depth + (b.depth - a.depth) * t;
    for (std::size_t c = 0; c < result.color.size(); ++c)
        result.color[c] = a.color[c] + (b.color[c] - a.color[c]) * t;
    return result;
}

std::vector<CameraVertex> clipDepth(const std::vector<CameraVertex>& polygon,
                                    const float plane, const bool keepGreater)
{
    std::vector<CameraVertex> clipped;
    if (polygon.empty()) return clipped;
    clipped.reserve(polygon.size() + 1);
    auto inside = [=](const CameraVertex& v) {
        return keepGreater ? v.depth >= plane : v.depth <= plane;
    };
    auto previous = polygon.back();
    bool previousInside = inside(previous);
    for (const auto& current : polygon) {
        const bool currentInside = inside(current);
        if (currentInside != previousInside) {
            const float denominator = current.depth - previous.depth;
            if (std::abs(denominator) > 1.0e-8f) {
                const float t = (plane - previous.depth) / denominator;
                clipped.push_back(interpolate(previous, current, t));
            }
        }
        if (currentInside) clipped.push_back(current);
        previous = current;
        previousInside = currentInside;
    }
    return clipped;
}

float edge(const ScreenVertex& a, const ScreenVertex& b, const float x, const float y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

void rasterizeTriangle(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c,
                       const std::uint32_t width, const std::uint32_t height,
                       std::vector<float>& depthBuffer, std::vector<std::uint8_t>& rgba,
                       TrackPreviewStats& stats)
{
    const float area = edge(a, b, c.x, c.y);
    if (!std::isfinite(area) || std::abs(area) < 1.0e-6f) return;
    const float minXf = (std::min)({a.x, b.x, c.x});
    const float maxXf = (std::max)({a.x, b.x, c.x});
    const float minYf = (std::min)({a.y, b.y, c.y});
    const float maxYf = (std::max)({a.y, b.y, c.y});
    if (maxXf < 0.0f || maxYf < 0.0f || minXf >= static_cast<float>(width) || minYf >= static_cast<float>(height)) return;
    const int minX = static_cast<int>((std::max)(0.0f, std::floor(minXf)));
    const int maxX = static_cast<int>((std::min)(static_cast<float>(width - 1), std::ceil(maxXf)));
    const int minY = static_cast<int>((std::max)(0.0f, std::floor(minYf)));
    const int maxY = static_cast<int>((std::min)(static_cast<float>(height - 1), std::ceil(maxYf)));
    const float inverseArea = 1.0f / area;
    bool wrote = false;
    for (int py = minY; py <= maxY; ++py) {
        for (int px = minX; px <= maxX; ++px) {
            const float x = static_cast<float>(px) + 0.5f;
            const float y = static_cast<float>(py) + 0.5f;
            const float wa = edge(b, c, x, y) * inverseArea;
            const float wb = edge(c, a, x, y) * inverseArea;
            const float wc = 1.0f - wa - wb;
            if (wa < -1.0e-5f || wb < -1.0e-5f || wc < -1.0e-5f) continue;
            const float inverseZ = wa / a.depth + wb / b.depth + wc / c.depth;
            if (!(inverseZ > 0.0f)) continue;
            const float z = 1.0f / inverseZ;
            const auto pixelIndex = static_cast<std::size_t>(py) * width + static_cast<std::size_t>(px);
            if (z >= depthBuffer[pixelIndex]) continue;
            depthBuffer[pixelIndex] = z;
            const float perspectiveWeights[]{wa / a.depth / inverseZ,
                                             wb / b.depth / inverseZ,
                                             wc / c.depth / inverseZ};
            const std::array<float, 3> channels{
                perspectiveWeights[0] * a.color[0] + perspectiveWeights[1] * b.color[0] + perspectiveWeights[2] * c.color[0],
                perspectiveWeights[0] * a.color[1] + perspectiveWeights[1] * b.color[1] + perspectiveWeights[2] * c.color[1],
                perspectiveWeights[0] * a.color[2] + perspectiveWeights[1] * b.color[2] + perspectiveWeights[2] * c.color[2]};
            const auto byte = [](const float value) {
                return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
            };
            const auto rgbaIndex = pixelIndex * 4;
            rgba[rgbaIndex + 0] = byte(channels[0]);
            rgba[rgbaIndex + 1] = byte(channels[1]);
            rgba[rgbaIndex + 2] = byte(channels[2]);
            rgba[rgbaIndex + 3] = 255;
            ++stats.depthPassedPixels;
            wrote = true;
        }
    }
    if (wrote) ++stats.rasterizedTriangles;
}
}

bool TrackPreviewRenderer::render(const TrackBatchGeometry& mesh, const std::uint32_t width,
                                  const std::uint32_t height, std::vector<std::uint8_t>& rgba,
                                  TrackPreviewStats& stats, std::string& error) const
{
    TrackPreviewCamera inspectionCamera;
    inspectionCamera.nearPlane = 0.0f; // internal sentinel: retain the bounds-framed inspection camera
    return render(mesh, inspectionCamera, width, height, rgba, stats, error);
}

bool TrackPreviewRenderer::render(const TrackBatchGeometry& mesh, const TrackPreviewCamera& requestedCamera,
                                  const std::uint32_t width, const std::uint32_t height,
                                  std::vector<std::uint8_t>& rgba, TrackPreviewStats& stats,
                                  std::string& error) const
{
    rgba.clear();
    stats = {};
    error.clear();
    if (width < 16 || height < 16 || std::uint64_t(width) * height > maximumPixels) {
        error = "Track preview dimensions are outside the 16..1280 by 16..896 safety range.";
        return false;
    }
    if (mesh.vertices.empty() || mesh.triangles.empty()) {
        error = "Track preview requires non-empty imported triangle geometry.";
        return false;
    }
    if (mesh.vertices.size() > 65'535 || mesh.triangles.size() > 131'070) {
        error = "Track preview geometry exceeds its source batch caps.";
        return false;
    }

    std::array<float, 3> minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& vertex : mesh.vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = (std::min)(minimum[axis], vertex.position[axis]);
            maximum[axis] = (std::max)(maximum[axis], vertex.position[axis]);
        }
    }
    for (const auto& triangle : mesh.triangles)
        for (const auto index : triangle.vertices)
            if (index >= mesh.vertices.size()) { error = "Track preview triangle references an invalid vertex."; return false; }

    stats.camera = requestedCamera;
    if (requestedCamera.nearPlane <= 0.0f) {
        stats.camera.target = {(minimum[0] + maximum[0]) * 0.5f,
                               (minimum[1] + maximum[1]) * 0.5f,
                               (minimum[2] + maximum[2]) * 0.5f};
        const float horizontalSpan = (std::max)(maximum[0] - minimum[0], maximum[1] - minimum[1]);
        const float distance = (std::max)(30.0f, horizontalSpan * 1.75f);
        const float pitch = stats.camera.pitchDegrees * 0.01745329251994329577f;
        stats.camera.eye = {stats.camera.target[0],
                            stats.camera.target[1] - distance * std::cos(pitch),
                            stats.camera.target[2] + distance * std::sin(pitch)};
    }
    if (!std::isfinite(stats.camera.eye[0]) || !std::isfinite(stats.camera.eye[1]) ||
        !std::isfinite(stats.camera.eye[2]) || !std::isfinite(stats.camera.target[0]) ||
        !std::isfinite(stats.camera.target[1]) || !std::isfinite(stats.camera.target[2]) ||
        !(stats.camera.verticalFovDegrees > 1.0f && stats.camera.verticalFovDegrees < 179.0f) ||
        !(stats.camera.nearPlane > 0.0f && stats.camera.farPlane > stats.camera.nearPlane)) {
        error = "Track preview camera pose or projection is invalid.";
        return false;
    }

    const std::uint64_t pixelCount = std::uint64_t(width) * height;
    const std::uint64_t rgbaBytes = pixelCount * 4;
    if (rgbaBytes > maximumImageBytes || rgbaBytes + pixelCount * sizeof(float) > maximumImageBytes * 2) {
        error = "Track preview framebuffer allocation exceeds its memory cap.";
        return false;
    }
    rgba.assign(static_cast<std::size_t>(rgbaBytes), 0);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = 4; rgba[i + 1] = 8; rgba[i + 2] = 13; rgba[i + 3] = 255;
    }
    std::vector<float> depthBuffer(static_cast<std::size_t>(pixelCount), std::numeric_limits<float>::infinity());

    const float radians = stats.camera.verticalFovDegrees * 0.01745329251994329577f;
    const float focal = (static_cast<float>(height) * 0.5f) / std::tan(radians * 0.5f);
    const auto subtract = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::array<float, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    };
    const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto normalize = [](const std::array<float, 3>& v) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return length > 1.0e-6f && std::isfinite(length)
            ? std::array<float, 3>{v[0] / length, v[1] / length, v[2] / length}
            : std::array<float, 3>{};
    };
    const auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::array<float, 3>{a[1] * b[2] - a[2] * b[1],
                                    a[2] * b[0] - a[0] * b[2],
                                    a[0] * b[1] - a[1] * b[0]};
    };
    const auto forward = normalize(subtract(stats.camera.target, stats.camera.eye));
    auto right = normalize(cross(forward, {0.0f, 0.0f, 1.0f}));
    if (right == std::array<float, 3>{}) right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const auto cameraUp = normalize(cross(right, forward));
    if (forward == std::array<float, 3>{} || right == std::array<float, 3>{} ||
        cameraUp == std::array<float, 3>{}) {
        error = "Track preview camera eye and target do not define a usable view basis.";
        return false;
    }
    const auto toCamera = [&](const std::uint32_t index) {
        const auto& source = mesh.vertices[index];
        CameraVertex result;
        const auto relative = subtract(source.position, stats.camera.eye);
        result.x = dot(relative, right);
        result.y = dot(relative, cameraUp);
        result.depth = dot(relative, forward);
        for (std::size_t channel = 0; channel < result.color.size(); ++channel)
            result.color[channel] = static_cast<float>((std::min)(255, int(source.color[channel]) * 2));
        return result;
    };
    const auto toScreen = [&](const CameraVertex& source) {
        return ScreenVertex{static_cast<float>(width) * 0.5f + focal * source.x / source.depth,
                            static_cast<float>(height) * 0.5f - focal * source.y / source.depth,
                            source.depth, source.color};
    };

    stats.inputTriangles = static_cast<std::uint32_t>(mesh.triangles.size());
    for (const auto& triangle : mesh.triangles) {
        std::vector<CameraVertex> polygon{toCamera(triangle.vertices[0]),
                                         toCamera(triangle.vertices[1]),
                                         toCamera(triangle.vertices[2])};
        if (std::any_of(polygon.begin(), polygon.end(), [&](const auto& v) { return v.depth < stats.camera.nearPlane; })) {
            ++stats.nearClippedTriangles;
            polygon = clipDepth(polygon, stats.camera.nearPlane, true);
        }
        if (polygon.size() < 3) continue;
        if (std::any_of(polygon.begin(), polygon.end(), [&](const auto& v) { return v.depth > stats.camera.farPlane; })) {
            ++stats.farClippedTriangles;
            polygon = clipDepth(polygon, stats.camera.farPlane, false);
        }
        if (polygon.size() < 3) continue;
        const auto first = toScreen(polygon[0]);
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
            rasterizeTriangle(first, toScreen(polygon[i]), toScreen(polygon[i + 1]), width, height,
                              depthBuffer, rgba, stats);
    }
    return true;
}
}
