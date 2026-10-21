#pragma once

#include "../../GameRuntime/PckArchive.h"
#include "../../GameRuntime/TrackGeometry.h"
#include "../../GameRuntime/VkModelGeometry.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>

namespace spyhunter::tools
{
// Embedded 3D inspection pane owned by SH Viewer. It accepts explicitly
// identified 3D resources and never routes a model through the track decoder.
class ModelViewerControl
{
public:
    void clear();
    bool openTrack(const std::filesystem::path& archivePath, const runtime::PckEntry& entry,
                   const runtime::TrackLoadOptions& options, std::string& error);
    bool openVkModel(const std::filesystem::path& archivePath, const runtime::PckEntry& entry,
                     const runtime::VkModelLoadOptions& options, std::string& error);
    void showUnsupportedResource(const std::filesystem::path& archivePath,
                                 const runtime::PckEntry& entry, const std::string& reason);
    bool writeGeometryDebugJson(const std::filesystem::path& outputPath, std::string& error) const;
    bool writeVkModelDebugJson(const std::filesystem::path& outputPath, std::string& error) const;
    bool active() const { return active_; }
    bool hasGeometry() const { return mesh_.has_value() || (model_ && !model_->drawables.empty()); }
    const runtime::TrackMesh* meshForDiagnostics() const { return mesh_ ? &*mesh_ : nullptr; }
    const runtime::VkModelGeometry* modelForDiagnostics() const { return model_ ? &*model_ : nullptr; }
    const std::string& status() const { return status_; }
    bool handleEvent(const SDL_Event& event, const SDL_FRect& viewerViewport);
    void render(SDL_Renderer* renderer, const SDL_FRect& viewport) const;

private:
    bool active_ = false;
    std::optional<runtime::TrackMesh> mesh_;
    std::optional<runtime::VkModelGeometry> model_;
    std::size_t selectedDrawable_ = 0;
    std::string status_;
    std::string resourceSummary_;
    float yaw_ = 0.0f;
    float elevation_ = 1.04719755f;
    float zoom_ = 1.0f;
    bool dragging_ = false;
    float lastMouseX_ = 0.0f;
    float lastMouseY_ = 0.0f;
    bool wireframe_ = false;
    bool vertexColors_ = false;
    bool diagnostics_ = false;
    bool bounds_ = false;
    bool vertexPoints_ = false;
};
}
