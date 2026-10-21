#include "Mission1Flythrough.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <span>

namespace spyhunter::game
{
namespace
{
constexpr std::uint64_t maximumAiBytes = 128 * 1024;
constexpr std::uint32_t maximumSplinePoints = 4096;
constexpr float tourDurationSeconds = 34.0f;

bool splinePosition(const Mission1Spline& spline, float distance,
                    std::array<float, 3>& position, std::array<float, 3>& direction);

class Reader
{
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::size_t position() const { return position_; }
    bool u16(std::uint16_t& value)
    {
        if (!available(2)) return false;
        value = static_cast<std::uint16_t>(bytes_[position_]) |
                static_cast<std::uint16_t>(bytes_[position_ + 1] << 8);
        position_ += 2;
        return true;
    }
    bool f32(float& value)
    {
        std::uint32_t bits = 0;
        if (!available(4)) return false;
        bits = static_cast<std::uint32_t>(bytes_[position_]) |
               (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24);
        position_ += 4;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }
    bool f64(double& value)
    {
        std::uint64_t bits = 0;
        if (!available(8)) return false;
        for (unsigned i = 0; i < 8; ++i) bits |= std::uint64_t(bytes_[position_ + i]) << (i * 8);
        position_ += 8;
        value = std::bit_cast<double>(bits);
        return std::isfinite(value);
    }
    bool name(std::string& value)
    {
        std::uint16_t length = 0;
        if (!u16(length) || length == 0 || length > 512 || !available(length)) return false;
        value.clear();
        value.reserve(length);
        for (std::uint16_t i = 0; i < length; ++i) {
            const auto c = bytes_[position_ + i];
            if (c != 0 && (c < 0x20 || c > 0x7e)) return false;
            if (c != 0) value.push_back(static_cast<char>(c));
        }
        position_ += length;
        return !value.empty();
    }
    bool skipFloats(std::uint32_t count)
    {
        float ignored = 0;
        for (std::uint32_t i = 0; i < count; ++i) if (!f32(ignored)) return false;
        return true;
    }
private:
    bool available(const std::size_t count) const { return count <= bytes_.size() - position_; }
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

bool readSpline(Reader& reader, Mission1Spline& spline)
{
    float storedLength = 0.0f;
    std::uint16_t pointCount = 0;
    if (!reader.name(spline.name) || !reader.f32(storedLength) || !reader.u16(pointCount) ||
        pointCount == 0 || pointCount > maximumSplinePoints) return false;
    spline.points.clear();
    spline.points.reserve(pointCount);
    for (std::uint16_t i = 0; i < pointCount; ++i) {
        Mission1SplinePoint point;
        for (auto& component : point.position) if (!reader.f32(component)) return false;
        for (auto& component : point.direction) if (!reader.f32(component)) return false;
        if (!reader.f32(point.length) || point.length < 0.0f) return false;
        spline.points.push_back(point);
    }
    return std::isfinite(storedLength);
}

bool readConnection(Reader& reader, const std::set<std::string>& validNames,
                    std::set<std::string>& connectionNames)
{
    std::string name;
    std::uint16_t nextCount = 0;
    if (!reader.name(name) || !validNames.contains(name) || !reader.u16(nextCount) || nextCount > 128)
        return false;
    connectionNames.insert(name);
    for (std::uint16_t i = 0; i < nextCount; ++i) {
        std::string next;
        if (!reader.name(next) || !validNames.contains(next)) return false;
    }
    return true;
}

bool parseAi(std::span<const std::uint8_t> bytes, Mission1Flythrough& result)
{
    Reader reader(bytes);
    std::uint16_t version = 0, pathCount = 0;
    if (!reader.u16(version) || version != 7 || !reader.u16(pathCount) || pathCount == 0 || pathCount > 128)
        return false;
    std::set<std::string> pathNames;
    std::set<std::string> laneNames;
    std::uint32_t laneCount = 0;
    for (std::uint16_t i = 0; i < pathCount; ++i) {
        std::string pathName;
        std::uint16_t kind = 0, flag = 0, count = 0, bounds = 0, densityA = 0, densityB = 0;
        if (!reader.name(pathName) || !pathNames.insert(pathName).second || !reader.u16(kind) ||
            !reader.u16(flag) || !reader.u16(count) || count > 32) return false;
        for (std::uint16_t lane = 0; lane < count; ++lane) {
            std::uint16_t laneType = 0, laneField = 0, locked = 0;
            if (!reader.u16(laneType) || !reader.u16(laneField) || !reader.u16(locked) || locked > 1)
                return false;
            if (locked) {
                std::uint16_t lockKind = 0;
                double slop = 0;
                if (!reader.u16(lockKind) || !reader.f64(slop)) return false;
            }
            Mission1Spline spline;
            if (!readSpline(reader, spline) || !laneNames.insert(spline.name).second) return false;
            ++laneCount;
        }
        if (!reader.u16(bounds) || bounds > maximumSplinePoints || !reader.skipFloats(std::uint32_t(bounds) * 7) ||
            !reader.u16(densityA) || !reader.u16(densityB)) return false;
    }
    std::set<std::string> foundPaths, foundLanes;
    for (std::uint16_t i = 0; i < pathCount; ++i)
        if (!readConnection(reader, pathNames, foundPaths)) return false;
    for (std::uint32_t i = 0; i < laneCount; ++i)
        if (!readConnection(reader, laneNames, foundLanes)) return false;
    if (foundPaths.size() != pathCount || foundLanes.size() != laneCount) return false;

    std::uint16_t otherCount = 0;
    if (!reader.u16(otherCount) || otherCount != 15) return false;
    std::vector<Mission1Spline> otherSplines;
    otherSplines.reserve(otherCount);
    for (std::uint16_t i = 0; i < otherCount; ++i) {
        Mission1Spline spline;
        if (!readSpline(reader, spline)) return false;
        otherSplines.push_back(std::move(spline));
    }

    std::array<Mission1CameraPath*, 2> cameras{&result.eye, &result.target};
    constexpr std::array<std::string_view, 2> expectedNames{"lineSplineNode31", "lineSplineNode30"};
    for (std::size_t cameraIndex = 0; cameraIndex < cameras.size(); ++cameraIndex) {
        std::uint16_t markerCount = 0;
        float startFrame = 0.0f, endFrame = 0.0f;
        auto& camera = *cameras[cameraIndex];
        if (!reader.u16(markerCount) || markerCount < 2 || markerCount > 30 ||
            !reader.f32(startFrame) || !reader.f32(endFrame) || !(endFrame > startFrame)) return false;
        camera.markers.clear();
        camera.markers.reserve(markerCount);
        float lastTime = -std::numeric_limits<float>::infinity();
        for (std::uint16_t i = 0; i < markerCount; ++i) {
            std::array<float, 2> marker{};
            if (!reader.f32(marker[0]) || !reader.f32(marker[1]) || !(marker[0] > lastTime)) return false;
            lastTime = marker[0];
            camera.markers.push_back(marker);
        }
        if (!readSpline(reader, camera.spline) || camera.spline.name != expectedNames[cameraIndex]) return false;
    }
    if (reader.position() != bytes.size()) return false;

    // Mission 1 ShProvingGrounds::loadIntroObjects source manifest: model and
    // distance constants retained by inspect_tour.py from the executable.
    struct Placement { std::uint8_t spline; const char* model; float distance; };
    static constexpr Placement placements[]{
        {0,"Van.vk",65.3853482f},{0,"BlueTruck.vk",143.8477661f},{0,"RedCar.vk",209.2331143f},{0,"BlueTruck.vk",326.9267412f},
        {1,"RedCar.vk",78.4566584f},{1,"BlueTruck.vk",196.1416460f},{1,"RedCar.vk",287.6744141f},{1,"BlueTruck.vk",339.9788530f},
        {2,"Caddy.vk",156.9248358f},{2,"BlueTruck.vk",261.5413929f},{2,"RedCar.vk",379.2350198f},
        {3,"RedCar.vk",115.4030074f},{3,"BlueTruck.vk",166.6932329f},{3,"RedCar.vk",256.4511275f},{3,"Van.vk",371.8541349f},
        {4,"BlueTruck.vk",10.0f},{4,"BlueTruck.vk",82.4845988f},{4,"BlueTruck.vk",154.9691976f},
        {5,"RedCar.vk",10.0f},{5,"RedCar.vk",92.8925323f},{5,"RedCar.vk",175.7850647f},{5,"RedCar.vk",258.6775970f},
        {6,"civ_boat_target.vk",21.9379242f},{6,"civ_boat_target.vk",87.7516968f},{6,"enemy_boat_target.vk",109.6896210f},{6,"civ_boat_target.vk",168.1907522f},{6,"civ_boat_target.vk",226.6918833f},
        {7,"civ_boat_target.vk",388.4678996f},{7,"enemy_boat_target.vk",360.7201925f},{7,"civ_boat_target.vk",339.9094121f},{7,"civ_boat_target.vk",263.6032176f},{7,"enemy_boat_target.vk",228.9185837f},{7,"civ_boat_target.vk",215.0447301f},
        {8,"civ_boat_target.vk",10.0f},{8,"civ_boat_target.vk",61.9808879f},{8,"civ_boat_target.vk",113.9617757f},{8,"civ_boat_target.vk",165.9426636f},{8,"civ_boat_target.vk",217.9235514f},{8,"civ_boat_target.vk",269.9044393f},{8,"civ_boat_target.vk",321.8853271f},{8,"civ_boat_target.vk",373.8662150f},{8,"civ_boat_target.vk",425.8471029f},{8,"civ_boat_target.vk",477.8279907f},{8,"civ_boat_target.vk",529.8088786f},{8,"civ_boat_target.vk",581.7897664f},{8,"civ_boat_target.vk",633.7706543f},{8,"civ_boat_target.vk",685.7515422f},{8,"civ_boat_target.vk",737.7324300f}
    };
    if (otherSplines.size() < 9) return false;
    result.temporaryObjects.clear();
    result.temporaryObjects.reserve(std::size(placements));
    for (const auto& placement : placements) {
        const auto& spline = otherSplines[placement.spline];
        std::array<float, 3> position{}, direction{};
        if (!splinePosition(spline, placement.distance, position, direction)) return false;
        result.temporaryObjects.push_back({placement.model, position, direction});
    }
    return result.temporaryObjects.size() == 48;
}

bool splinePosition(const Mission1Spline& spline, const float distance,
                    std::array<float, 3>& position, std::array<float, 3>& direction)
{
    if (spline.points.empty() || !std::isfinite(distance) || distance < 0.0f) return false;
    float accumulated = 0.0f;
    for (const auto& point : spline.points) {
        if (point.length > 0.0f && distance <= accumulated + point.length) {
            const float along = distance - accumulated;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                position[axis] = point.position[axis] + point.direction[axis] * along;
                direction[axis] = point.direction[axis];
            }
            return true;
        }
        accumulated += point.length;
    }
    const auto& last = spline.points.back();
    const float along = (std::max)(0.0f, distance - (accumulated - last.length));
    for (std::size_t axis = 0; axis < 3; ++axis) {
        position[axis] = last.position[axis] + last.direction[axis] * (std::min)(along, last.length);
        direction[axis] = last.direction[axis];
    }
    return true;
}

bool evaluateCameraPath(const Mission1CameraPath& path, const float seconds,
                        std::array<float, 3>& position)
{
    if (path.markers.size() < 2 || path.spline.points.empty()) return false;
    float distance = path.markers.front()[1];
    if (seconds >= path.markers.back()[0]) distance = path.markers.back()[1];
    else if (seconds > path.markers.front()[0]) {
        for (std::size_t i = 0; i + 1 < path.markers.size(); ++i) {
            const auto& a = path.markers[i];
            const auto& b = path.markers[i + 1];
            if (seconds < b[0]) {
                const float amount = std::clamp((seconds - a[0]) / (b[0] - a[0]), 0.0f, 1.0f);
                distance = a[1] + (b[1] - a[1]) * amount;
                break;
            }
        }
    }
    std::array<float, 3> direction{};
    return splinePosition(path.spline, distance, position, direction);
}

std::array<float, 3> normalized(std::array<float, 3> value)
{
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!(length > 1.0e-6f) || !std::isfinite(length)) return {};
    for (auto& component : value) component /= length;
    return value;
}

void appendBox(runtime::TrackBatchGeometry& scene, const Mission1IntroObject& object)
{
    const bool boat = object.model.find("boat") != std::string::npos;
    const bool truck = object.model.find("Truck") != std::string::npos || object.model.find("Van") != std::string::npos;
    const float length = boat ? 14.0f : truck ? 12.0f : 8.0f;
    const float width = boat ? 5.0f : truck ? 5.5f : 4.0f;
    const float height = boat ? 3.0f : truck ? 5.0f : 3.0f;
    auto forward = normalized({object.direction[0], object.direction[1], 0.0f});
    if (forward == std::array<float, 3>{}) forward = {0.0f, 1.0f, 0.0f};
    const std::array<float, 3> right{forward[1], -forward[0], 0.0f};
    const std::array<float, 3> halfForward{forward[0] * length * 0.5f, forward[1] * length * 0.5f, 0.0f};
    const std::array<float, 3> halfRight{right[0] * width * 0.5f, right[1] * width * 0.5f, 0.0f};
    const auto base = static_cast<std::uint32_t>(scene.vertices.size());
    std::array<float, 3> color = boat ? std::array<float, 3>{45, 125, 175} :
        object.model.find("Blue") != std::string::npos ? std::array<float, 3>{55, 105, 205} :
        object.model.find("enemy") != std::string::npos ? std::array<float, 3>{205, 60, 50} :
        std::array<float, 3>{185, 65, 52};
    for (unsigned z = 0; z < 2; ++z) for (unsigned f = 0; f < 2; ++f) for (unsigned r = 0; r < 2; ++r) {
        runtime::TrackVertex vertex;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = object.position[axis] + (f ? halfForward[axis] : -halfForward[axis]) +
                (r ? halfRight[axis] : -halfRight[axis]);
        }
        vertex.position[2] += z ? height : 0.0f;
        for (std::size_t channel = 0; channel < 3; ++channel)
            vertex.color[channel] = static_cast<std::uint8_t>(color[channel]);
        vertex.color[3] = 255;
        scene.vertices.push_back(vertex);
    }
    constexpr std::array<std::array<std::uint32_t, 3>, 12> faces{{
        {{0,1,3}},{{0,3,2}},{{4,6,7}},{{4,7,5}},{{0,4,5}},{{0,5,1}},
        {{2,3,7}},{{2,7,6}},{{0,2,6}},{{0,6,4}},{{1,5,7}},{{1,7,3}}
    }};
    for (const auto& face : faces) scene.triangles.push_back({{base + face[0], base + face[1], base + face[2]}});
}
}

bool loadMission1Flythrough(const std::filesystem::path& aiPath,
                            Mission1Flythrough& result, std::string& error)
{
    error.clear();
    std::error_code ec;
    const auto size = std::filesystem::file_size(aiPath, ec);
    if (ec || size < 32 || size > maximumAiBytes) {
        error = "PG1.AI is missing or outside the bounded Mission 1 v7 reader size.";
        return false;
    }
    std::ifstream file(aiPath, std::ios::binary);
    if (!file) { error = "Could not open PG1.AI for camera-tour setup."; return false; }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "PG1.AI ended before the bounded camera-tour read completed.";
        return false;
    }
    result = {};
    if (!parseAi(bytes, result)) {
        error = "PG1.AI failed Mission 1 v7 structure, reference, camera spline, or exact-end validation.";
        return false;
    }
    result.sourceBytes = size;
    return true;
}

bool evaluateMission1Flythrough(const Mission1Flythrough& tour, const float elapsedSeconds,
                                runtime::TrackPreviewCamera& camera, std::string& error)
{
    error.clear();
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0f || tour.sourceBytes == 0) {
        error = "Mission 1 camera-tour time or source payload is invalid.";
        return false;
    }
    const auto time = (std::min)(elapsedSeconds, tourDurationSeconds);
    if (!evaluateCameraPath(tour.eye, time, camera.eye) ||
        !evaluateCameraPath(tour.target, time, camera.target)) {
        error = "Mission 1 camera spline evaluation failed.";
        return false;
    }
    camera.verticalFovDegrees = 70.0f;
    camera.nearPlane = 0.5f;
    camera.farPlane = 1200.0f;
    return true;
}

bool appendMission1IntroProxies(const Mission1Flythrough& tour,
                               runtime::TrackBatchGeometry& scene, std::string& error)
{
    error.clear();
    if (tour.temporaryObjects.size() != 48 || scene.vertices.size() + 48 * 8 > 65'535 ||
        scene.triangles.size() + 48 * 12 > 131'070) {
        error = "Mission 1 intro proxy count or geometry exceeds the diagnostic scene cap.";
        return false;
    }
    for (const auto& object : tour.temporaryObjects) appendBox(scene, object);
    return true;
}
}
