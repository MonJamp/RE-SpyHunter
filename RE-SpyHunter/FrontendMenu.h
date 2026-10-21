#pragma once

#include "../GameRuntime/CutscenePlayer.h"
#include "../GameRuntime/PackedTexture.h"
#include "../GameRuntime/StillTexture.h"
#include "../GameRuntime/VkfTextureFont.h"
#include "MissionLevelLoading.h"
#include "../GameUI/MenuSelection.h"
#include "../GameUI/MenuAxisRepeat.h"
#include "../GameUI/AlphaClipMask.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace spyhunter::game
{
class FrontendMenu
{
public:
    static constexpr std::size_t profileCount = 3;
    enum class Screen { profileSelect, nameEntry, mainMenu, twoPlayer, systemOptions, dossier,
                        missionSelect, briefing, deleteProfile, settings, controller, extras,
                        musicPlayer, moviePlayer, cheatGrid, controller1Options, controller2Options,
                        missionLoading, levelPreview };

    FrontendMenu(SDL_Renderer* renderer, const std::filesystem::path& root,
                 std::filesystem::path profilePath, std::filesystem::path backgroundPath);
    bool load(std::string& error, bool debugProfile = false,
              std::optional<Screen> debugStartScreen = std::nullopt);
    void setMissionLoadRoot(const std::filesystem::path& root) { missionLoadRoot_ = root; }
    void setMissionLoadCapturePause(std::chrono::milliseconds pause) { missionLoadCapturePause_ = pause; }
    void handleEvent(const SDL_Event& event);
    void update();
    void render() const;

    Screen screen() const { return screen_; }
    std::size_t selectedProfile() const { return profileSelection_.index(); }
    std::size_t selectedMainItem() const { return mainSelection_.index(); }
    std::size_t selectedSubmenuItem() const { return submenuSelection_.index(); }
    std::size_t itemCount() const { return screen_ == Screen::profileSelect ? profileCount :
        screen_ == Screen::mainMenu ? 4 : screen_ == Screen::nameEntry ? 0 : menuItems_.size(); }
    const std::array<std::string, profileCount>& profiles() const { return profiles_; }
    const std::string& pendingName() const { return pendingName_; }
    const std::string& status() const { return status_; }
    struct Options {
        // Initial local profile values mirror the latest Settings reference;
        // they are not asserted as the original game's fresh-profile defaults.
        std::array<int, 5> values{1, 10, 7, 10, 1};
        std::array<std::size_t, 2> controllerSchemes{};
        bool operator==(const Options&) const = default;
    };
    const Options& options() const { return options_[profileSelection_.index()]; }
    struct MissionProgress {
        int objectivePoints = 0;
        bool allLevelsOverride = false;
        std::array<bool, 14> primaryObjectivesComplete{};
    };
    bool setMissionProgress(std::size_t profile, MissionProgress progress);
    std::size_t musicLayout() const { return musicLayout_; }

private:
    void moveSelection(int direction);
    void accept();
    void back();
    void open(Screen destination);
    void configure(Screen destination, std::size_t selection = 0);
    void horizontal(int direction);
    void secondary();
    void appendNameCharacter(char character);
    void startMissionLoad();
    void cancelMissionLoad();
    void drawMissionLoading() const;
    void drawMissionLevelPreview() const;
    bool saveProfiles(std::string& error) const;
    void drawCentered(std::string_view text, float yFromBottom, float scale = 1.0f) const;

    SDL_Renderer* renderer_;
    std::filesystem::path root_;
    std::filesystem::path profilePath_;
    std::filesystem::path backgroundPath_;
    std::filesystem::path missionLoadRoot_;
    std::chrono::milliseconds missionLoadCapturePause_{};
    runtime::VkfTextureFont font_;
    runtime::VkfTextureFont profileFont_;
    runtime::VkfTextureFont missionLoadingTitleFont_;
    runtime::VkfTextureFont missionLoadingFont_;
    runtime::StillTexture missionLoadingBackground_;
    runtime::PackedTexture missionLoadingBar_;
    runtime::PackedTexture navIcon_;
    runtime::PackedTexture acceptIcon_;
    runtime::PackedTexture deleteIcon_;
    runtime::PackedTexture backIcon_;
    runtime::PackedTexture menuHighlightbar_;
    runtime::PackedTexture menuLightbar_;
    runtime::PackedTexture menuEmptyBox_;
    runtime::PackedTexture menuFilledBox_;
    runtime::PackedTexture dossierCheckedBox_;
    runtime::PackedTexture dossierUncheckedBox_;
    runtime::PackedTexture profileLightbar_;
    std::array<std::array<runtime::PackedTexture, 3>, 14> missionScenes_;
    std::array<std::size_t, 14> missionSceneSlots_{};
    std::array<double, 14> missionSceneChangedAt_{};
    std::array<SDL_Point, 14> missionCardPositions_{};
    Uint64 missionCardLastUpdatedAtMs_ = 0;
    std::array<runtime::PackedTexture, 13> missionAccessLabels_;
    runtime::PackedTexture missionBlue_;
    runtime::PackedTexture missionOverlay4_;
    runtime::PackedTexture missionOverlay2_;
    runtime::PackedTexture missionOverlay3_;
    runtime::PackedTexture rendezvousLine_;
    ui::AlphaClipMask missionMask_;
    runtime::CutsceneSession background_;
    bool backgroundLoaded_ = false;
    std::array<std::string, profileCount> profiles_{};
    Screen screen_ = Screen::profileSelect;
    ui::MenuSelection profileSelection_{profileCount};
    ui::MenuSelection mainSelection_{4};
    ui::MenuSelection submenuSelection_{1};
    std::vector<Screen> menuStack_;
    std::vector<std::size_t> menuSelectionStack_;
    std::string menuTitle_;
    std::vector<std::string_view> menuItems_;
    std::vector<Screen> menuDestinations_;
    std::string pendingName_;
    std::string status_;
    std::array<Options, profileCount> options_{};
    std::array<MissionProgress, profileCount> missionProgress_{};
    Options entryOptions_;
    std::size_t mission_ = 0;
    std::size_t dossierOpenMissionCount_ = 1;
    std::array<int, 14> dossierElapsedSeconds_{};
    std::array<std::array<bool, 7>, 14> dossierObjectives_{};
    std::size_t musicLayout_ = 0;
    ui::MenuAxisRepeat horizontalAxis_{0.95f, 0.6, 0.19};
    ui::MenuAxisRepeat verticalAxis_{0.95f, 0.6, 0.19};
    mutable std::mutex missionLoadMutex_;
    MissionLoadStage missionLoadStage_ = MissionLoadStage::staticArchive;
    std::string missionLoadItem_;
    std::string missionLoadError_;
    std::uint64_t missionLoadCompletedBytes_ = 0;
    std::uint64_t missionLoadTotalBytes_ = 0;
    bool missionLoadFinished_ = false;
    bool missionLoadSucceeded_ = false;
    std::optional<runtime::TrackGeometryImport> missionTrack_;
    // Declared last so it is joined before the callback state above is destroyed.
    std::jthread missionLoadWorker_;
};
}
