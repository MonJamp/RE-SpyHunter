#pragma once

#include "../../GameRuntime/PckArchive.h"
#include "../../GameRuntime/TrackGeometry.h"

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
    void showUnsupportedResource(const std::filesystem::path& archivePath,
                                 const runtime::PckEntry& entry, const std::string& reason);
    bool writeGeometryDebugJson(const std::filesystem::path& outputPath, std::string& error) const;
    bool active() const { return active_; }
    bool hasGeometry() const { return mesh_.has_value(); }
    const runtime::TrackMesh* meshForDiagnostics() const { return mesh_ ? &*mesh_ : nullptr; }
    const std::string& status() const { return status_; }
    bool handleEvent(const SDL_Event& event);
    void render(SDL_Renderer* renderer, const SDL_FRect& viewport) const;

private:
    bool active_ = false;
    std::optional<runtime::TrackMesh> mesh_;
    std::string status_;
    std::string resourceSummary_;
    float yaw_ = 0.0f;
    float zoom_ = 1.0f;
    bool wireframe_ = false;
    bool vertexColors_ = false;
    bool diagnostics_ = false;
    bool bounds_ = false;
    bool vertexPoints_ = false;
};
}
