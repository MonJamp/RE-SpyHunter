#include "ModelViewerControl.h"
#include "../../GameRuntime/MeshTopology.h"

#include "../../GUI/Gui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace spyhunter::tools
{
namespace
{
struct ProjectedVertex { SDL_FPoint point{}; float depth = 0.0f; SDL_Color color{}; };
struct Triangle { std::array<ProjectedVertex, 3> vertices{}; float depth = 0.0f; };

float detailsPanelWidth(const float viewportWidth, const bool diagnostics)
{
    const float preferredWidth = diagnostics ? 400.0f : 360.0f;
    const float viewportFraction = diagnostics ? 0.48f : 0.42f;
    return (std::min)(preferredWidth, viewportWidth * viewportFraction);
}

SDL_FRect sceneRect(const SDL_FRect& viewport, const bool diagnostics)
{
    const float panelWidth = detailsPanelWidth(viewport.w, diagnostics);
    return {viewport.x + 16.0f, viewport.y + 42.0f,
            viewport.w - panelWidth - 32.0f, viewport.h - 88.0f};
}

SDL_FRect detailsRect(const SDL_FRect& viewport, const bool diagnostics)
{
    const SDL_FRect scene = sceneRect(viewport, diagnostics);
    return {scene.x + scene.w + 16.0f, scene.y,
            detailsPanelWidth(viewport.w, diagnostics), scene.h};
}

SDL_Rect sceneClipRect(const SDL_FRect& scene)
{
    const int left = static_cast<int>(std::ceil(scene.x));
    const int top = static_cast<int>(std::ceil(scene.y));
    const int right = static_cast<int>(std::floor(scene.x + scene.w));
    const int bottom = static_cast<int>(std::floor(scene.y + scene.h));
    return {left, top, (std::max)(0, right - left), (std::max)(0, bottom - top)};
}

template<typename Vertex>
SDL_FPoint project(const Vertex& source, const std::array<float, 3>& center,
                   const float scale, const SDL_FRect& viewport, const float yaw, const float elevation)
{
    const float x = source.position[0] - center[0];
    const float y = source.position[1] - center[1];
    const float z = source.position[2] - center[2];
    const float azimuth = yaw + 0.78539816f;
    const float azimuthCos = std::cos(azimuth), azimuthSin = std::sin(azimuth);
    const float elevationCos = std::cos(elevation), elevationSin = std::sin(elevation);
    const float screenRight = x * azimuthCos - y * azimuthSin;
    const float screenDepth = x * azimuthSin + y * azimuthCos;
    // Orthographic orbit camera. At the reset elevation this preserves the prior
    // isometric view while allowing mouse drag to change azimuth and elevation.
    return {viewport.x + viewport.w * 0.5f + screenRight * scale,
            viewport.y + viewport.h * 0.52f + (screenDepth * elevationCos - z * elevationSin) * scale};
}

template<typename Mesh>
void renderTrack(SDL_Renderer* renderer, const Mesh& mesh, const SDL_FRect& viewport,
                 const bool wireframe, const bool vertexColors, const bool showBounds,
                 const bool showVertices, const float yaw, const float elevation, const float zoom)
{
    if (mesh.vertices.empty() || viewport.w <= 0 || viewport.h <= 0) return;
    std::array<float, 3> minimum = mesh.vertices.front().position;
    std::array<float, 3> maximum = minimum;
    for (const auto& vertex : mesh.vertices) for (std::size_t axis = 0; axis < 3; ++axis) {
        minimum[axis] = (std::min)(minimum[axis], vertex.position[axis]);
        maximum[axis] = (std::max)(maximum[axis], vertex.position[axis]);
    }
    std::array<float, 3> center{};
    for (std::size_t axis = 0; axis < 3; ++axis) center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
    const float extent = (std::max)({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2], 0.001f});
    const float scale = (std::min)(viewport.w, viewport.h) * 0.68f / extent * zoom;

    std::vector<Triangle> triangles;
    if (!runtime::validateStripLayout(mesh.stripLengths, mesh.vertices.size())) return;
    runtime::forEachStripTriangle(mesh.stripLengths, [&](std::size_t, const auto& indices) {
        Triangle triangle;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& source = mesh.vertices[indices[corner]];
            auto& output = triangle.vertices[corner];
            output.point = project(source, center, scale, viewport, yaw, elevation);
            output.depth = source.position[0] + source.position[1] + source.position[2];
            triangle.depth += output.depth;
            output.color = vertexColors
                ? SDL_Color{source.color[0], source.color[1], source.color[2], 255}
                : SDL_Color{172, 185, 198, 255};
        }
        triangle.depth /= 3.0f;
        triangles.push_back(triangle);
    });
    std::stable_sort(triangles.begin(), triangles.end(), [](const Triangle& a, const Triangle& b) { return a.depth < b.depth; });

    if (!wireframe) {
        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
        vertices.reserve(triangles.size() * 3);
        indices.reserve(triangles.size() * 3);
        for (const auto& triangle : triangles) for (const auto& vertex : triangle.vertices) {
            indices.push_back(static_cast<int>(vertices.size()));
            vertices.push_back({vertex.point,
                {vertex.color.r / 255.0f, vertex.color.g / 255.0f, vertex.color.b / 255.0f, vertex.color.a / 255.0f}, {}});
        }
        SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
    }
    if (wireframe) {
        SDL_SetRenderDrawColor(renderer, 40, 44, 50, 255);
        for (const auto& triangle : triangles) for (std::size_t edge = 0; edge < 3; ++edge) {
            const auto& from = triangle.vertices[edge].point;
            const auto& to = triangle.vertices[(edge + 1) % 3].point;
            SDL_RenderLine(renderer, from.x, from.y, to.x, to.y);
        }
    }
    if (showBounds) {
        std::array<SDL_FPoint, 8> corners{};
        for (unsigned mask = 0; mask < corners.size(); ++mask) {
            const std::array<float, 3> point{
                (mask & 1) ? maximum[0] : minimum[0],
                (mask & 2) ? maximum[1] : minimum[1],
                (mask & 4) ? maximum[2] : minimum[2]};
            corners[mask] = project(runtime::TrackVertex{point}, center, scale, viewport, yaw, elevation);
        }
        SDL_SetRenderDrawColor(renderer, 220, 54, 48, 255);
        for (unsigned mask = 0; mask < corners.size(); ++mask)
            for (unsigned axis = 0; axis < 3; ++axis) {
                const unsigned other = mask ^ (1u << axis);
                if (mask < other)
                    SDL_RenderLine(renderer, corners[mask].x, corners[mask].y, corners[other].x, corners[other].y);
            }
    }
    if (showVertices) {
        SDL_SetRenderDrawColor(renderer, 24, 92, 220, 255);
        for (const auto& vertex : mesh.vertices) {
            const auto point = project(vertex, center, scale, viewport, yaw, elevation);
            SDL_RenderPoint(renderer, point.x, point.y);
        }
    }
}

template<typename Mesh>
std::size_t triangleSlots(const Mesh& mesh)
{
    std::size_t count = 0;
    for (const auto length : mesh.stripLengths) count += length - 2;
    return count;
}

std::string jsonString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char digits[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(digits[character >> 4]);
                escaped.push_back(digits[character & 0x0f]);
            } else escaped.push_back(static_cast<char>(character));
        }
    }
    escaped.push_back('"');
    return escaped;
}

struct MeshDiagnostics
{
    std::array<float, 3> minimum{}, maximum{};
    std::array<float, 2> uvMinimum{}, uvMaximum{};
    std::array<std::uint8_t, 4> colorMinimum{}, colorMaximum{};
    std::size_t degenerateTriangles = 0;
};

template<typename Mesh>
MeshDiagnostics diagnose(const Mesh& mesh)
{
    MeshDiagnostics result;
    result.minimum = result.maximum = mesh.vertices.front().position;
    result.uvMinimum = result.uvMaximum = mesh.vertices.front().uv;
    result.colorMinimum = result.colorMaximum = mesh.vertices.front().color;
    std::size_t base = 0;
    for (const auto& stripLength : mesh.stripLengths) {
        for (std::size_t i = 0; i < stripLength; ++i) {
            const auto& vertex = mesh.vertices[base + i];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                result.minimum[axis] = (std::min)(result.minimum[axis], vertex.position[axis]);
                result.maximum[axis] = (std::max)(result.maximum[axis], vertex.position[axis]);
            }
            for (std::size_t axis = 0; axis < 2; ++axis) {
                result.uvMinimum[axis] = (std::min)(result.uvMinimum[axis], vertex.uv[axis]);
                result.uvMaximum[axis] = (std::max)(result.uvMaximum[axis], vertex.uv[axis]);
            }
            for (std::size_t channel = 0; channel < 4; ++channel) {
                result.colorMinimum[channel] = (std::min)(result.colorMinimum[channel], vertex.color[channel]);
                result.colorMaximum[channel] = (std::max)(result.colorMaximum[channel], vertex.color[channel]);
            }
        }
        for (std::size_t i = 2; i < stripLength; ++i) {
            const auto& a = mesh.vertices[base + i - 2].position;
            const auto& b = mesh.vertices[base + i - 1].position;
            const auto& c = mesh.vertices[base + i].position;
            const double abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
            const double acx = c[0] - a[0], acy = c[1] - a[1], acz = c[2] - a[2];
            const double nx = aby * acz - abz * acy;
            const double ny = abz * acx - abx * acz;
            const double nz = abx * acy - aby * acx;
            if (nx * nx + ny * ny + nz * nz <= 1.0e-20) ++result.degenerateTriangles;
        }
        base += stripLength;
    }
    return result;
}
}

void ModelViewerControl::clear()
{
    active_ = false;
    mesh_.reset();
    model_.reset();
    selectedDrawable_ = 0;
    status_.clear();
    resourceSummary_.clear();
    yaw_ = 0.0f;
    elevation_ = 1.04719755f;
    zoom_ = 1.0f;
    dragging_ = false;
    wireframe_ = vertexColors_ = false;
    diagnostics_ = bounds_ = vertexPoints_ = false;
}

bool ModelViewerControl::openTrack(const std::filesystem::path& archivePath, const runtime::PckEntry& entry,
                                   const runtime::TrackLoadOptions& options, std::string& error)
{
    clear();
    runtime::TrackMesh mesh;
    if (!runtime::loadPg1TrackMesh(archivePath, entry, options, mesh, error)) return false;
    mesh_ = std::move(mesh);
    active_ = true;
    status_ = "PG1 track fixture loaded; diagnostic host view.";
    return true;
}

bool ModelViewerControl::openVkModel(const std::filesystem::path& archivePath, const runtime::PckEntry& entry,
                                     const runtime::VkModelLoadOptions& options, std::string& error)
{
    clear();
    runtime::VkModelGeometry model;
    if (!runtime::loadVkModelGeometry(archivePath, entry, options, model, error)) return false;
    model_ = std::move(model);
    active_ = true;
    status_ = "Raw vkPS2TriStrips geometry loaded; model hierarchy transforms and shaders are not applied.";
    return true;
}

bool ModelViewerControl::writeGeometryDebugJson(const std::filesystem::path& outputPath, std::string& error) const
{
    error.clear();
    if (!mesh_ || mesh_->vertices.empty()) {
        error = "No decoded geometry is available to export.";
        return false;
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open debug JSON output path: " + outputPath.string();
        return false;
    }
    const auto& mesh = *mesh_;
    output << std::setprecision(9)
        << "{\n  \"format\": \"SHViewer geometry debug v1\",\n"
        << "  \"interpretation\": \"observed PG1 vkTrack v3.0.85 / BSP v7; host diagnostic view\",\n"
        << "  \"archive\": " << jsonString(mesh.archivePath) << ",\n"
        << "  \"member\": " << jsonString(mesh.memberName) << ",\n"
        << "  \"member_offset\": " << mesh.memberOffset << ",\n"
        << "  \"batch_offset\": " << mesh.batchOffset << ",\n"
        << "  \"vertex_data_offset\": " << mesh.vertexDataOffset << ",\n"
        << "  \"vertex_stride_bytes\": 24,\n"
        << "  \"leaf_index\": " << mesh.leafIndex << ",\n"
        << "  \"batch_index\": " << mesh.batchIndex << ",\n"
        << "  \"bsp_version\": " << mesh.bspVersion << ",\n"
        << "  \"texture_index\": " << mesh.textureIndex << ",\n"
        << "  \"texture_name\": " << jsonString(mesh.textureName) << ",\n"
        << "  \"units\": \"unknown; source coordinates retained\",\n"
        << "  \"depth_method\": \"painter-order approximation\",\n"
        << "  \"strip_lengths\": [";
    for (std::size_t i = 0; i < mesh.stripLengths.size(); ++i)
        output << (i ? ", " : "") << mesh.stripLengths[i];
    output << "],\n  \"vertices\": [\n";
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& vertex = mesh.vertices[i];
        output << "    {\"index\": " << i << ", \"source_offset\": " << mesh.vertexDataOffset + i * 24
            << ", \"position\": ["
            << vertex.position[0] << ", " << vertex.position[1] << ", " << vertex.position[2]
            << "], \"color_rgba\": [" << static_cast<unsigned>(vertex.color[0]) << ", "
            << static_cast<unsigned>(vertex.color[1]) << ", " << static_cast<unsigned>(vertex.color[2]) << ", "
            << static_cast<unsigned>(vertex.color[3]) << "], \"uv\": [" << vertex.uv[0] << ", " << vertex.uv[1] << "]}"
            << (i + 1 == mesh.vertices.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"triangles\": [\n";
    std::size_t triangleIndex = 0;
    const auto totalTriangles = triangleSlots(mesh);
    runtime::forEachStripTriangle(mesh.stripLengths, [&](std::size_t strip, const auto& indices) {
        output << "    {\"strip\": " << strip << ", \"indices\": ["
            << indices[0] << ", " << indices[1] << ", " << indices[2] << "]}"
            << (++triangleIndex == totalTriangles ? "\n" : ",\n");
    });
    output << "  ]\n}\n";
    if (!output) {
        error = "Failed while writing debug JSON output: " + outputPath.string();
        return false;
    }
    return true;
}

bool ModelViewerControl::writeVkModelDebugJson(const std::filesystem::path& outputPath, std::string& error) const
{
    error.clear();
    if (!model_ || model_->drawables.empty()) {
        error = "No decoded vkModel geometry is available to export.";
        return false;
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open vkModel debug JSON output path: " + outputPath.string();
        return false;
    }
    output << std::setprecision(9)
        << "{\n  \"format\": \"SHViewer vkModel geometry debug v1\",\n"
        << "  \"interpretation\": \"observed vkPS2TriStrips versioned streams; raw local drawable geometry\",\n"
        << "  \"archive\": " << jsonString(model_->archivePath) << ",\n"
        << "  \"member\": " << jsonString(model_->memberName) << ",\n"
        << "  \"member_offset\": " << model_->memberOffset << ",\n"
        << "  \"member_size\": " << model_->memberSize << ",\n"
        << "  \"model_version\": " << jsonString(std::to_string((model_->modelVersion >> 24) & 0xff) + "." +
            std::to_string((model_->modelVersion >> 16) & 0xff) + "." + std::to_string(model_->modelVersion & 0xffff)) << ",\n"
        << "  \"node_transforms_applied\": false,\n  \"shader_texture_resolved\": false,\n"
        << "  \"unsupported_drawable_records\": " << model_->unsupportedDrawableRecords << ",\n"
        << "  \"drawables\": [\n";
    for (std::size_t d = 0; d < model_->drawables.size(); ++d) {
        const auto& drawable = model_->drawables[d];
        output << "    {\"object_index\": " << drawable.objectIndex
            << ", \"serialization_version\": " << jsonString(std::to_string((drawable.serializationVersion >> 24) & 0xff) + "." +
                std::to_string((drawable.serializationVersion >> 16) & 0xff) + "." + std::to_string(drawable.serializationVersion & 0xffff))
            << ", \"header_offset\": " << model_->memberOffset + drawable.entryHeaderOffset
            << ", \"vertex_flags\": " << drawable.vertexFlags
            << ", \"vertex_data_offset\": " << model_->memberOffset + drawable.vertexDataOffset
            << ", \"has_flag_0x2\": " << (drawable.hasAttributeFlag2 ? "true" : "false")
            << ", \"has_flag_0x4\": " << (drawable.hasAttributeFlag4 ? "true" : "false")
            << ", \"attribute_flag_semantics_confirmed\": false"
            << ", \"has_uv\": " << (drawable.hasUv ? "true" : "false") << ",\n"
            << "      \"strip_lengths\": [";
        for (std::size_t i = 0; i < drawable.stripLengths.size(); ++i)
            output << (i ? ", " : "") << drawable.stripLengths[i];
        output << "],\n      \"vertices\": [\n";
        for (std::size_t i = 0; i < drawable.vertices.size(); ++i) {
            const auto& vertex = drawable.vertices[i];
            const auto vertexOffset = model_->memberOffset + drawable.vertexDataOffset + i *
                (12 + (drawable.hasAttributeFlag2 ? 4 : 0) + (drawable.hasAttributeFlag4 ? 4 : 0) + (drawable.hasUv ? 8 : 0));
            output << "        {\"index\": " << i << ", \"source_offset\": " << vertexOffset << ", \"position\": ["
                << vertex.position[0] << ", " << vertex.position[1] << ", " << vertex.position[2] << "]";
            if (drawable.hasAttributeFlag2)
                output << ", \"attribute_flag_0x2_raw\": [" << static_cast<unsigned>(vertex.attributeFlag2[0]) << ", "
                    << static_cast<unsigned>(vertex.attributeFlag2[1]) << ", " << static_cast<unsigned>(vertex.attributeFlag2[2]) << ", "
                    << static_cast<unsigned>(vertex.attributeFlag2[3]) << "]";
            if (drawable.hasAttributeFlag4)
                output << ", \"attribute_flag_0x4_raw\": [" << static_cast<unsigned>(vertex.attributeFlag4[0]) << ", "
                    << static_cast<unsigned>(vertex.attributeFlag4[1]) << ", " << static_cast<unsigned>(vertex.attributeFlag4[2]) << ", "
                    << static_cast<unsigned>(vertex.attributeFlag4[3]) << "]";
            if (drawable.hasUv) output << ", \"uv\": [" << vertex.uv[0] << ", " << vertex.uv[1] << "]";
            output << "}" << (i + 1 == drawable.vertices.size() ? "\n" : ",\n");
        }
        output << "      ]\n    }" << (d + 1 == model_->drawables.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    if (!output) {
        error = "Failed while writing vkModel debug JSON output: " + outputPath.string();
        return false;
    }
    return true;
}

void ModelViewerControl::showUnsupportedResource(const std::filesystem::path& archivePath,
                                                 const runtime::PckEntry& entry, const std::string& reason)
{
    clear();
    active_ = true;
    status_ = reason;
    resourceSummary_ = archivePath.filename().string() + " / " + entry.name + " | " + entry.type +
        " v" + std::to_string((entry.serializationVersion >> 24) & 0xff) + "." +
        std::to_string((entry.serializationVersion >> 16) & 0xff) + "." +
        std::to_string(entry.serializationVersion & 0xffff) + " | bytes [" +
        std::to_string(entry.offset) + ", " + std::to_string(entry.offset + entry.size) + ")";
}

bool ModelViewerControl::handleEvent(const SDL_Event& event, const SDL_FRect& viewerViewport)
{
    if (!active_) return false;
    const SDL_FRect scene = sceneRect(viewerViewport, diagnostics_);
    const auto insideScene = [&](const float x, const float y) {
        return x >= scene.x && x < scene.x + scene.w && y >= scene.y && y < scene.y + scene.h;
    };
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        const float x = event.button.x / spyhunter::gui::defaultUiScale;
        const float y = event.button.y / spyhunter::gui::defaultUiScale;
        if (!insideScene(x, y)) return false;
        dragging_ = true;
        lastMouseX_ = x;
        lastMouseY_ = y;
        SDL_CaptureMouse(true);
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && dragging_) {
        dragging_ = false;
        SDL_CaptureMouse(false);
        return true;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && dragging_) {
        dragging_ = false;
        SDL_CaptureMouse(false);
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && dragging_) {
        const float x = event.motion.x / spyhunter::gui::defaultUiScale;
        const float y = event.motion.y / spyhunter::gui::defaultUiScale;
        constexpr float sensitivity = 0.008f;
        yaw_ = std::remainder(yaw_ + (x - lastMouseX_) * sensitivity, 6.28318531f);
        elevation_ = (std::clamp)(elevation_ - (y - lastMouseY_) * sensitivity,
                                  0.08726646f, 1.48352986f);
        lastMouseX_ = x;
        lastMouseY_ = y;
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        const float x = event.wheel.mouse_x / spyhunter::gui::defaultUiScale;
        const float y = event.wheel.mouse_y / spyhunter::gui::defaultUiScale;
        if (!insideScene(x, y)) return false;
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        zoom_ = (std::clamp)(zoom_ * std::pow(1.12f, direction * event.wheel.y), 0.08f, 12.0f);
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return false;
    switch (event.key.key) {
    case SDLK_W: wireframe_ = !wireframe_; return true;
    case SDLK_C:
        if (model_) return true; // The raw model attribute flag is not yet confirmed as color.
        vertexColors_ = !vertexColors_;
        return true;
    case SDLK_F3: diagnostics_ = !diagnostics_; return true;
    case SDLK_F4: bounds_ = !bounds_; return true;
    case SDLK_V: vertexPoints_ = !vertexPoints_; return true;
    case SDLK_LEFTBRACKET:
        if (model_ && !model_->drawables.empty()) {
            selectedDrawable_ = (selectedDrawable_ + model_->drawables.size() - 1) % model_->drawables.size();
            return true;
        }
        return false;
    case SDLK_RIGHTBRACKET:
        if (model_ && !model_->drawables.empty()) {
            selectedDrawable_ = (selectedDrawable_ + 1) % model_->drawables.size();
            return true;
        }
        return false;
    case SDLK_LEFT: yaw_ -= 0.08f; return true;
    case SDLK_RIGHT: yaw_ += 0.08f; return true;
    case SDLK_UP: elevation_ = (std::min)(1.48352986f, elevation_ + 0.06f); return true;
    case SDLK_DOWN: elevation_ = (std::max)(0.08726646f, elevation_ - 0.06f); return true;
    case SDLK_EQUALS: case SDLK_KP_PLUS: zoom_ = (std::min)(12.0f, zoom_ * 1.1f); return true;
    case SDLK_MINUS: case SDLK_KP_MINUS: zoom_ = (std::max)(0.08f, zoom_ / 1.1f); return true;
    case SDLK_HOME: yaw_ = 0.0f; elevation_ = 1.04719755f; zoom_ = 1.0f; return true;
    default: return false;
    }
}

void ModelViewerControl::render(SDL_Renderer* renderer, const SDL_FRect& viewport) const
{
    if (!active_ || viewport.w <= 0 || viewport.h <= 0) return;
    spyhunter::gui::panel(renderer, viewport, {23, 27, 32, 255});
    SDL_SetRenderDrawColor(renderer, 226, 233, 243, 255);
    spyhunter::gui::text(renderer, viewport.x + 16, viewport.y + 12, "3D RESOURCE INSPECTOR | HOST DIAGNOSTIC VIEW");
    const SDL_FRect scene = sceneRect(viewport, diagnostics_);
    const SDL_FRect details = detailsRect(viewport, diagnostics_);
    spyhunter::gui::panel(renderer, scene, {232, 235, 238, 255});
    spyhunter::gui::panel(renderer, details, {27, 34, 44, 255});
    SDL_SetRenderDrawColor(renderer, 229, 236, 246, 255);
    if (mesh_ || (model_ && !model_->drawables.empty())) {
        float y = scene.y + 16;
        const float textX = details.x + 14.0f;
        const float textRight = details.x + details.w - 12.0f;
        const auto line = [&](const std::string& value) {
            const auto clipped = spyhunter::gui::shorten(value,
                static_cast<std::size_t>((std::max)(12.0f, (textRight - textX) / 8.0f)));
            SDL_SetRenderDrawColor(renderer, 229, 236, 246, 255);
            spyhunter::gui::text(renderer, textX, y, clipped);
            y += diagnostics_ ? 18.0f : 20.0f;
        };
        if (mesh_) {
            const auto& mesh = *mesh_;
            const SDL_Rect sceneClip = sceneClipRect(scene);
            SDL_SetRenderClipRect(renderer, &sceneClip);
            renderTrack(renderer, mesh, scene, wireframe_, vertexColors_, bounds_, vertexPoints_, yaw_, elevation_, zoom_);
            SDL_SetRenderClipRect(renderer, nullptr);
            line("Type: vkTrack v3.0.85");
            line("Internal BSP: " + std::to_string(mesh.bspVersion));
            line("Member: " + spyhunter::gui::shorten(mesh.memberName, 34));
            line("Archive: " + spyhunter::gui::shorten(std::filesystem::path(mesh.archivePath).filename().string(), 34));
            line("Member bytes: " + std::to_string(mesh.memberOffset));
            line("Batch offset: " + std::to_string(mesh.batchOffset));
            line("Vertex data offset: " + std::to_string(mesh.vertexDataOffset));
            line("Leaf / batch: " + std::to_string(mesh.leafIndex) + " / " + std::to_string(mesh.batchIndex));
            line("Vertices: " + std::to_string(mesh.vertices.size()));
            line("Strips: " + std::to_string(mesh.stripLengths.size()));
            line("Triangle slots: " + std::to_string(triangleSlots(mesh)));
            line("Texture ref: " + std::to_string(mesh.textureIndex));
            line("Texture: " + (mesh.textureName.empty() ? std::string("<null>") : mesh.textureName));
            line("Texture unresolved");
            line("Coordinates retained; units unknown");
            line("Depth uses painter approximation");
        } else {
            const auto& drawable = model_->drawables[selectedDrawable_];
            const SDL_Rect sceneClip = sceneClipRect(scene);
            SDL_SetRenderClipRect(renderer, &sceneClip);
            renderTrack(renderer, drawable, scene, wireframe_, false,
                        bounds_, vertexPoints_, yaw_, elevation_, zoom_);
            SDL_SetRenderClipRect(renderer, nullptr);
            line("Type: vkModel v" + std::to_string((model_->modelVersion >> 24) & 0xff) + "." +
                 std::to_string((model_->modelVersion >> 16) & 0xff) + "." + std::to_string(model_->modelVersion & 0xffff));
            line("Member: " + spyhunter::gui::shorten(model_->memberName, 34));
            line("Archive: " + spyhunter::gui::shorten(std::filesystem::path(model_->archivePath).filename().string(), 34));
            line("Member bytes: " + std::to_string(model_->memberOffset) + " + " + std::to_string(model_->memberSize));
            line("Drawable: " + std::to_string(selectedDrawable_ + 1) + " / " + std::to_string(model_->drawables.size()) +
                 " (object " + std::to_string(drawable.objectIndex) + ")");
            line("Geometry header: " + std::to_string(model_->memberOffset + drawable.entryHeaderOffset));
            line("Vertex data: " + std::to_string(model_->memberOffset + drawable.vertexDataOffset));
            line("Vertices / strips: " + std::to_string(drawable.vertices.size()) + " / " + std::to_string(drawable.stripLengths.size()));
            line("Triangle slots: " + std::to_string(triangleSlots(drawable)));
            line("Flags: 0x" + [&] { std::ostringstream out; out << std::hex << drawable.vertexFlags; return out.str(); }());
            line(std::string("Attributes: position") + (drawable.hasAttributeFlag2 ? ", flag 0x2 raw" : "") +
                 (drawable.hasAttributeFlag4 ? ", flag 0x4 raw" : "") + (drawable.hasUv ? ", UV" : ""));
            line("Attribute flag meanings unresolved");
            line("Node transforms not applied");
            line("Shader / texture unresolved");
            line("W: wireframe | C: color flag unresolved");
            line("[ / ]: select drawable");
        }
        line("Left-drag: orbit | wheel: zoom");
        line("Arrows: orbit / elevation | +/-: zoom");
        line("Home: reset | F3: diagnostics");
        line("F4: bounds | V: vertex points");
        const auto drawGeometryDiagnostics = [&](const auto& geometry) {
            if (!diagnostics_) return;
            const auto stats = diagnose(geometry);
            const auto pair = [](const float minimum, const float maximum) {
                return std::to_string(minimum) + " .. " + std::to_string(maximum);
            };
            line("X: " + pair(stats.minimum[0], stats.maximum[0]));
            line("Y: " + pair(stats.minimum[1], stats.maximum[1]));
            line("Z: " + pair(stats.minimum[2], stats.maximum[2]));
            line("U: " + pair(stats.uvMinimum[0], stats.uvMaximum[0]));
            line("V: " + pair(stats.uvMinimum[1], stats.uvMaximum[1]));
            line("Degenerate triangle slots: " + std::to_string(stats.degenerateTriangles));
            line("Yaw / elev (rad) / zoom (x): " + std::to_string(yaw_) + " / " +
                 std::to_string(elevation_) + " / " + std::to_string(zoom_));
        };
        if (mesh_) drawGeometryDiagnostics(*mesh_);
        else drawGeometryDiagnostics(model_->drawables[selectedDrawable_]);
    } else {
        const float textX = details.x + 14.0f;
        SDL_SetRenderDrawColor(renderer, 38, 46, 58, 255);
        spyhunter::gui::text(renderer, scene.x + 20, scene.y + 28, "Resource identified; geometry is not available for preview.");
        spyhunter::gui::text(renderer, scene.x + 20, scene.y + 52, "Parser status and source range are shown at right.");
        SDL_SetRenderDrawColor(renderer, 229, 236, 246, 255);
        spyhunter::gui::text(renderer, textX, scene.y + 16, "PARSER STATUS");
        spyhunter::gui::text(renderer, textX, scene.y + 42, spyhunter::gui::shorten(status_, 42));
        spyhunter::gui::text(renderer, textX, scene.y + 76, spyhunter::gui::shorten(resourceSummary_, 42));
        spyhunter::gui::text(renderer, textX, scene.y + 108, "Source member remains read-only.");
    }
}
}
