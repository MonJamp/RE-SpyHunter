#include "FrontendMenu.h"
#include "FrontendCatalogue.h"
#include "../GameUI/MenuDrawing.h"
#include "../GameRuntime/PckArchive.h"
#include "../GameRuntime/TrackPreviewRenderer.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>

namespace spyhunter::game
{
namespace
{
constexpr float canvasWidth = 640.0f;
constexpr float canvasHeight = 448.0f;
constexpr std::array<std::string_view, 4> mainItems{
    "OPERATIONS", "TWO - PLAYER", "SYSTEM OPTIONS", "DOSSIER"
};
// Original float bits from color initialization at SLUS_200.56 VA 0x0019AF08.
constexpr SDL_Color sourceColor(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
{
    const auto byte = [](std::uint32_t bits) { return static_cast<Uint8>(std::bit_cast<float>(bits) * 255.0f + 0.5f); };
    return {byte(r), byte(g), byte(b), byte(a)};
}

// Channels are the recovered setColor() trunc(component × 128) values.
// Expanding GS 0..128 to SDL 0..255 is a host mapping; final GS texture
// modulation, blend and framebuffer output remain uncalibrated.
constexpr SDL_Color missionTextColor(const int r, const int g, const int b, const int a)
{
    const auto byte = [](const int channel) {
        const auto gs = (std::clamp)(channel, 0, 128);
        return static_cast<Uint8>((gs * 255 + 64) / 128);
    };
    return {byte(r), byte(g), byte(b), byte(a)};
}
constexpr std::array<SDL_FRect, 4> mainSelectorBounds{{
    {180.0f, 148.0f, 285.0f, 30.0f},
    {170.0f, 188.0f, 300.0f, 30.0f},
    {162.0f, 234.0f, 325.0f, 30.0f},
    {210.0f, 278.0f, 220.0f, 30.0f}
}};

struct MenuNode {
    FrontendMenu::Screen screen;
    std::string_view title;
    std::span<const std::string_view> labels;
    std::span<const FrontendMenu::Screen> destinations;
};

constexpr std::array<FrontendMenu::Screen, 0> noDestinations{};
constexpr std::array<std::string_view, 0> noLabels{};
constexpr std::array<std::string_view, 3> systemOptionsLabels{"SETTINGS", "CONTROLLER", "EXTRAS"};
constexpr std::array<FrontendMenu::Screen, 3> systemOptionsDestinations{
    FrontendMenu::Screen::settings, FrontendMenu::Screen::controller, FrontendMenu::Screen::extras};
constexpr std::array<std::string_view, 3> extrasLabels{"MUSIC PLAYER", "MOVIE PLAYER", "CHEAT GRID"};
constexpr std::array<FrontendMenu::Screen, 3> extrasDestinations{
    FrontendMenu::Screen::musicPlayer, FrontendMenu::Screen::moviePlayer, FrontendMenu::Screen::cheatGrid};
constexpr std::array<std::string_view, 2> controllerLabels{"PLAYER 1", "PLAYER 2"};
constexpr std::array<FrontendMenu::Screen, 2> controllerDestinations{
    FrontendMenu::Screen::controller1Options, FrontendMenu::Screen::controller2Options};
constexpr std::array<std::string_view, 3> controllerOptionsLabels{"STANDARD", "REVERSED", "FLIPPED"};
constexpr std::array<std::string_view, 5> settingsLabels{"SOUND", "FX VOLUME", "MUSIC VOLUME", "VOICE VOLUME", "VIBRATION"};
constexpr std::array<int, 14> dossierObjectiveCounts{7, 4, 4, 4, 4, 4, 7, 4, 4, 5, 6, 4, 4, 4};
constexpr float dossierTableX = 45.0f;
constexpr float dossierFirstRowBaseline = 317.0f;
// The reference GS frame is 640x480 while this app renders at 640x448.
constexpr float dossierRowStep = 16.0f;
constexpr float dossierTimeColumnStep = 250.0f;
constexpr float dossierObjectivesColumnStep = 340.0f;
// Source ShMSSSlider enum order, recovered from its draw and cycle helpers.
constexpr std::array<std::string_view, 4> soundModeLabels{"MONO", "STEREO", "SURROUND", "HEADPHONES"};
constexpr std::array<std::string_view, 3> briefingLabels{"OBJECTIVES", "WEAPONS", "ENEMIES"};
constexpr std::array<std::string_view, 2> deleteLabels{"YES", "NO"};
// SLUS_200.56 ShMenu1::buildStatus, requirements SEL-04. The local profile
// currently has no gameplay progress store, so these source-derived point
// thresholds are evaluated against zero points and incomplete prerequisites.
constexpr std::array<int, 14> missionPointThresholds{0, 4, 7, 11, 14, 18, 22, 27, 32, 39, 42, 48, 53, 57};
constexpr std::array<std::string_view, 14> missionScenePrefixes{
    "pg1_", "ge1_", "ve1_", "kw1_", "fra_", "pan1_", "pg2_", "eng_", "ge2_", "pan2_",
    "fr2_", "kw2_", "ve2_", "mid_"};
constexpr std::array<std::string_view, 13> missionAccessMembers{
    "menu_opr_access_04.rgba", "menu_opr_access_07.rgba", "menu_opr_access_11.rgba",
    "menu_opr_access_14.rgba", "menu_opr_access_18.rgba", "menu_opr_access_22.rgba",
    "menu_opr_access_27.rgba", "menu_opr_access_32.rgba", "menu_opr_access_39.rgba",
    "menu_opr_access_42.rgba", "menu_opr_access_48.rgba", "menu_opr_access_53.rgba",
    "menu_opr_access_57.rgba"};
constexpr std::array<int, 14> missionAllowanceSeconds{480, 420, 360, 420, 420, 420, 420, 420, 420, 360, 360, 360, 300, 540};

int linearTrack(const int target, const int current, const float deltaSeconds)
{
    const float difference = static_cast<float>(target - current);
    if (std::abs(difference) <= 0.000001f) return current;
    const float step = std::copysign(400.0f * std::min(deltaSeconds, 0.05f), difference);
    if (std::abs(step) > std::abs(difference)) return target;
    return static_cast<int>(std::nearbyint(static_cast<float>(current) + step));
}

int missionAllowance(const std::size_t mission, const bool allLevelsOverride)
{
    if (allLevelsOverride) return 1000;
    return mission < missionAllowanceSeconds.size() ? missionAllowanceSeconds[mission] : 0;
}

struct MissionAccess
{
    bool open = false;
    bool insufficientPoints = false;
    bool previousPrimaryIncomplete = false;
};

MissionAccess missionAccess(const std::size_t mission, const FrontendMenu::MissionProgress& progress)
{
    if (mission >= missionPointThresholds.size()) return {};
    const bool firstMission = mission == 0;
    const bool insufficientPoints = !progress.allLevelsOverride && !firstMission &&
        progress.objectivePoints < missionPointThresholds[mission];
    const bool previousPrimaryIncomplete = !progress.allLevelsOverride && !firstMission &&
        !progress.primaryObjectivesComplete[mission - 1];
    return {
        progress.allLevelsOverride || firstMission ||
            (!insufficientPoints && !previousPrimaryIncomplete),
        insufficientPoints,
        previousPrimaryIncomplete
    };
}

bool hasSecondController()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_free(ids);
    return count >= 2;
}

std::optional<MenuNode> menuNode(const FrontendMenu::Screen screen)
{
    using Screen = FrontendMenu::Screen;
    switch (screen) {
    case Screen::twoPlayer: return MenuNode{screen, "TWO PLAYER", catalogue::missions, noDestinations};
    case Screen::systemOptions: return MenuNode{screen, "SYSTEM OPTIONS", systemOptionsLabels, systemOptionsDestinations};
    case Screen::dossier: return MenuNode{screen, "DOSSIER", noLabels, noDestinations};
    case Screen::extras: return MenuNode{screen, "EXTRAS", extrasLabels, extrasDestinations};
    case Screen::controller: return MenuNode{screen, "CONTROLLER", controllerLabels, controllerDestinations};
    case Screen::controller1Options:
        return MenuNode{screen, "CONTROLLER 1 OPTIONS", controllerOptionsLabels, noDestinations};
    case Screen::controller2Options:
        return MenuNode{screen, "CONTROLLER 2 OPTIONS", controllerOptionsLabels, noDestinations};
    case Screen::missionSelect: return MenuNode{screen, "MISSION SELECT", catalogue::missions, noDestinations};
    case Screen::briefing: return MenuNode{screen, "MISSION BRIEFING", briefingLabels, noDestinations};
    case Screen::settings: return MenuNode{screen, "SETTINGS", settingsLabels, noDestinations};
    case Screen::musicPlayer: return MenuNode{screen, "MUSIC PLAYER", catalogue::music, noDestinations};
    case Screen::moviePlayer: return MenuNode{screen, "MOVIE PLAYER", catalogue::movies, noDestinations};
    case Screen::cheatGrid: return MenuNode{screen, "CHEAT GRID", catalogue::rewards, noDestinations};
    case Screen::deleteProfile: return MenuNode{screen, "DELETE PROFILE?", deleteLabels, noDestinations};
    default: return std::nullopt;
    }
}

bool isCarousel(FrontendMenu::Screen screen)
{
    using Screen = FrontendMenu::Screen;
    return screen == Screen::missionSelect || screen == Screen::twoPlayer ||
        screen == Screen::musicPlayer || screen == Screen::moviePlayer || screen == Screen::cheatGrid;
}

bool isAccept(const SDL_Event& event)
{
    return (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.key == SDLK_RETURN || event.key.key == SDLK_X)) ||
           (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH);
}

bool isBack(const SDL_Event& event)
{
    return (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_ESCAPE) ||
           (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH);
}
}

FrontendMenu::FrontendMenu(SDL_Renderer* renderer, const std::filesystem::path& root,
                           std::filesystem::path profilePath, std::filesystem::path backgroundPath)
    : renderer_(renderer), root_(root), profilePath_(std::move(profilePath)),
      backgroundPath_(std::move(backgroundPath)) {}

bool FrontendMenu::setMissionProgress(const std::size_t profile, MissionProgress progress)
{
    if (profile >= missionProgress_.size() || progress.objectivePoints < 0) return false;
    missionProgress_[profile] = std::move(progress);
    return true;
}

bool FrontendMenu::load(std::string& error, const bool debugProfile,
                        const std::optional<Screen> debugStartScreen)
{
    const auto frontendAssetRoot = root_ / "DATA";
    if (!font_.loadFromPackedArchive(frontendAssetRoot / "MENU.PCK", "Eurobet.vkf", renderer_, error))
        return false;
    if (!profileFont_.loadFromPackedArchive(frontendAssetRoot / "MENU.PCK", "euroStile12.vkf", renderer_, error))
        return false;
    const auto assetRoot = frontendAssetRoot;
    const auto menuUiArchive = assetRoot / "MENU.PCK";
    const auto stencil = runtime::loadPackedTexturePixels(menuUiArchive, "menu_stencil1.rgba", error);
    if (!stencil) return false;
    if (stencil->width != 128 || stencil->height != 512) {
        error = "Mission Select stencil must be 128 x 512 for the USA layout.";
        return false;
    }
    // ShMenu::buildNewMenuSprites places the 128x512 side texture at x=0 and a
    // horizontally mirrored copy at x=512. Alpha-only writes combine with the
    // transparent top/bottom clear sprites at (57,1,135,90) and (57,359,135,90).
    // This binary host mask approximates the GS destination-alpha test; it is
    // distinct from the 70%-alpha black panel drawn through it below.
    std::vector<std::uint8_t> visible(640 * 448, 0);
    for (int y = 0; y < 448; ++y) {
        for (int x = 0; x < 640; ++x) {
            bool value = false;
            if (y >= 91 && y < 360) {
                const int stencilX = x < 128 ? x : x >= 512 ? 639 - x : -1;
                value = stencilX < 0 || stencil->rgba[(static_cast<std::size_t>(y) * 128 + stencilX) * 4 + 3] == 255;
            }
            // Untextured alpha-zero clear quads are submitted after the sides.
            if ((x >= 57 && x < 192 && y >= 1 && y < 91) ||
                (x >= 57 && x < 192 && y >= 359 && y < 449)) value = false;
            visible[static_cast<std::size_t>(y) * 640 + x] = value ? 1 : 0;
        }
    }
    if (!missionMask_.assign(640, 448, visible)) {
        error = "Unable to construct Mission Select visibility mask.";
        return false;
    }
    for (std::size_t index = 0; index < missionScenes_.size(); ++index) {
        for (std::size_t frame = 0; frame < missionScenes_[index].size(); ++frame) {
            const auto member = std::string(missionScenePrefixes[index]) + "scene" +
                std::to_string(frame + 1) + ".rgb";
            if (!missionScenes_[index][frame].load(menuUiArchive, member, renderer_, error)) return false;
        }
    }
    for (std::size_t index = 0; index < missionAccessLabels_.size(); ++index)
        if (!missionAccessLabels_[index].load(menuUiArchive, missionAccessMembers[index], renderer_, error)) return false;
    if (!missionBlue_.load(menuUiArchive, "pg1_blue.rgb", renderer_, error) ||
        !missionOverlay4_.load(menuUiArchive, "menu_oper_staticimage_4.rgba", renderer_, error) ||
        !missionOverlay2_.load(menuUiArchive, "menu_oper_staticimage_2.rgba", renderer_, error) ||
        !missionOverlay3_.load(menuUiArchive, "menu_oper_staticimage_3.rgba", renderer_, error) ||
        !rendezvousLine_.load(menuUiArchive, "menu_orangeline_rendev_int.rgba", renderer_, error)) return false;
    if (!menuHighlightbar_.load(menuUiArchive, "menu_highlightbar.rgba", renderer_, error))
        return false;
    if (!missionLoadingTitleFont_.loadFromPackedArchive(assetRoot / "SYSTEM.PCK", "hudFont.vkf",
                                                        renderer_, error) ||
        !missionLoadingFont_.loadFromPackedArchive(assetRoot / "SYSTEM.PCK", "hudFont.vkf",
                                                   0.53f, renderer_, error))
        return false;
    if (!missionLoadingBackground_.load(renderer_, assetRoot / "LOADING.VKI", error))
        return false;
    runtime::PckArchive hudArchive;
    if (!hudArchive.open(assetRoot / "SYSTEM.PCK", error)) return false;
    const auto hudBar = std::find_if(hudArchive.entries().begin(), hudArchive.entries().end(),
        [](const runtime::PckEntry& entry) {
            return entry.type == "vkDMATexture" &&
                (entry.name == "messagebar.rgb" || entry.loadName == "new_hud/messagebar.rgb");
        });
    if (hudBar == hudArchive.entries().end()) {
        error = "Original loading bar messagebar.rgb was not found in SYSTEM.PCK.";
        return false;
    }
    std::ifstream hudArchiveStream(assetRoot / "SYSTEM.PCK", std::ios::binary);
    if (!hudArchiveStream || !missionLoadingBar_.load(hudArchiveStream, hudBar->offset, hudBar->size,
            renderer_, error, SDL_SCALEMODE_NEAREST))
        return false;
    if (!menuLightbar_.load(assetRoot / "SYSTEM.PCK", "menu_lightbar_1pc.rgba", renderer_, error))
        return false;
    const auto menuArchive = assetRoot / "SYSTEM.PCK";
    if (!menuEmptyBox_.load(menuArchive, "menu_empty_box.rgba", renderer_, error) ||
        !menuFilledBox_.load(menuArchive, "menu_filled_box.rgba", renderer_, error))
        return false;
    if (!dossierCheckedBox_.load(menuUiArchive, "menu_checkedbox.rgba", renderer_, error) ||
        !dossierUncheckedBox_.load(menuUiArchive, "menu_uncheckedbox.rgba", renderer_, error))
        return false;
    if (!profileLightbar_.load(root_ / "DATA" / "MENU.PCK", "menu_lightbar_1pc_long.rgba", renderer_, error))
        return false;
    const auto systemArchive = assetRoot / "SYSTEM.PCK";
    if (!navIcon_.load(systemArchive, "menu_buttons_nav_all.rgba", renderer_, error) ||
        !acceptIcon_.load(systemArchive, "menu_buttons_x.rgba", renderer_, error) ||
        !deleteIcon_.load(systemArchive, "menu_buttons_sq.rgba", renderer_, error) ||
        !backIcon_.load(systemArchive, "menu_buttons_tri.rgba", renderer_, error))
        return false;
    if (std::filesystem::is_regular_file(backgroundPath_)) {
        backgroundLoaded_ = background_.load(renderer_, {backgroundPath_, false, false, false, true, ""}, error);
        if (!backgroundLoaded_) return false;
    } else {
        SDL_Log("Converted MBACK background is unavailable: %s", backgroundPath_.string().c_str());
    }
    std::ifstream input(profilePath_);
    std::string line;
    for (std::size_t index = 0; index < profiles_.size() && std::getline(input, line); ++index) {
        line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char character) {
            return character < 0x20 || character > 0x7e;
        }), line.end());
        profiles_[index] = line.substr(0, 12);
    }
    std::string marker;
    if (input >> marker && marker == "OPTIONS_V1") {
        for (auto& options : options_) {
            Options loaded;
            bool valid = true;
            for (std::size_t index = 0; index < loaded.values.size(); ++index) {
                const int maximum = index == 0 ? 3 : index == 4 ? 1 : 10;
                if (!(input >> loaded.values[index]) || loaded.values[index] < 0 || loaded.values[index] > maximum) valid = false;
            }
            for (auto& scheme : loaded.controllerSchemes)
                if (!(input >> scheme) || scheme > 2) valid = false;
            if (valid) options = loaded;
        }
    }
    if (debugProfile) profiles_[0] = "DEBUG";
    if (debugStartScreen) {
        Screen parent = Screen::systemOptions;
        std::size_t parentSelection = 0;
        switch (*debugStartScreen) {
        case Screen::missionSelect: parent = Screen::mainMenu; parentSelection = 0; break;
        case Screen::dossier: parent = Screen::mainMenu; parentSelection = 3; break;
        case Screen::settings: parentSelection = 0; break;
        case Screen::controller: parentSelection = 1; break;
        case Screen::extras: parentSelection = 2; break;
        case Screen::musicPlayer: parent = Screen::extras; parentSelection = 0; break;
        case Screen::moviePlayer: parent = Screen::extras; parentSelection = 1; break;
        case Screen::cheatGrid: parent = Screen::extras; parentSelection = 2; break;
        default:
            error = "The debug start screen must be a supported frontend screen.";
            return false;
        }
        if (!debugProfile) {
            error = "A debug start screen requires the isolated debug profile.";
            return false;
        }
        profileSelection_.select(0);
        if (parent == Screen::mainMenu) {
            mainSelection_.select(parentSelection);
            configure(parent);
        } else {
            configure(parent, parentSelection);
        }
        open(*debugStartScreen);
    }
    error.clear();
    return true;
}

void FrontendMenu::update()
{
    if (backgroundLoaded_ && screen_ != Screen::missionLoading && screen_ != Screen::levelPreview) background_.update();
    if (screen_ == Screen::missionLoading) {
        bool preparePreview = false;
        {
            std::lock_guard lock(missionLoadMutex_);
            std::string label;
            switch (missionLoadStage_) {
            case MissionLoadStage::staticArchive: label = "READING STATIC ARCHIVE"; break;
            case MissionLoadStage::staticLevelData: label = "READING WATER DATA"; break;
            case MissionLoadStage::dynamicArchive: label = "READING DYNAMIC ARCHIVE"; break;
            case MissionLoadStage::levelInitializationData: label = "READING LEVEL SETUP DATA"; break;
            case MissionLoadStage::trackGeometry: label = "BUILDING TRACK GEOMETRY"; break;
            }
            status_ = missionLoadFinished_ ?
                (missionLoadSucceeded_ ? "TRACK IMPORT COMPLETE; BUILDING DEPTH VIEW" : missionLoadError_) :
                (missionLoadItem_.empty() ? label : label + " - " + missionLoadItem_);
            preparePreview = missionLoadFinished_ && missionLoadSucceeded_ && missionTrack_ && !missionTrackPreviewTexture_;
        }
        if (preparePreview) {
            std::string previewError;
            if (prepareMissionLevelPreview(previewError)) screen_ = Screen::levelPreview;
            else {
                std::lock_guard lock(missionLoadMutex_);
                missionLoadSucceeded_ = false;
                missionLoadError_ = std::move(previewError);
                status_ = missionLoadError_;
            }
        }
        return;
    }
    const auto nowMs = SDL_GetTicks();
    const double nowSeconds = static_cast<double>(nowMs) / 1000.0;
    if (screen_ == Screen::missionSelect) {
        const float deltaSeconds = missionCardLastUpdatedAtMs_ == 0 || nowMs < missionCardLastUpdatedAtMs_
            ? 0.0f : static_cast<float>(nowMs - missionCardLastUpdatedAtMs_) / 1000.0f;
        missionCardLastUpdatedAtMs_ = nowMs;
        const auto selected = submenuSelection_.index();
        constexpr std::array<int, 5> offsets{-1, 0, 1, 2, 3};
        constexpr std::array<int, 5> targets{28, 120, 200, 278, 360};
        for (std::size_t slot = 0; slot < offsets.size(); ++slot) {
            const auto mission = static_cast<std::size_t>((static_cast<int>(selected) + offsets[slot] +
                static_cast<int>(missionCardPositions_.size())) % static_cast<int>(missionCardPositions_.size()));
            auto& position = missionCardPositions_[mission];
            position.x = linearTrack(60, position.x, deltaSeconds);
            position.y = linearTrack(targets[slot], position.y, deltaSeconds);
        }
        const auto immediateMission = (selected + missionCardPositions_.size() - 2) % missionCardPositions_.size();
        missionCardPositions_[immediateMission] = {60, 28};
        for (std::size_t mission = 0; mission < missionSceneSlots_.size(); ++mission) {
            const double interval = static_cast<double>(std::rand() % 10) + 1.0;
            if (nowSeconds - missionSceneChangedAt_[mission] > interval) {
                missionSceneSlots_[mission] = (missionSceneSlots_[mission] + 1) % 5;
                missionSceneChangedAt_[mission] = nowSeconds;
            }
        }
    }
    if (screen_ == Screen::controller2Options && !hasSecondController()) back();
    const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
    if (const auto direction = horizontalAxis_.poll(now)) horizontal(direction);
    if (const auto direction = verticalAxis_.poll(now)) moveSelection(direction);
}

void FrontendMenu::moveSelection(const int direction)
{
    if (screen_ == Screen::profileSelect) {
        profileSelection_.move(direction);
    } else if (screen_ == Screen::mainMenu || menuNode(screen_)) {
        if (screen_ == Screen::briefing || screen_ == Screen::dossier) return;
        if (screen_ != Screen::mainMenu) {
            submenuSelection_.move(screen_ == Screen::cheatGrid ? direction * 4 : direction);
            status_.clear();
            return;
        }
        mainSelection_.move(direction);
        status_.clear();
    }
}

void FrontendMenu::appendNameCharacter(char character)
{
    if (pendingName_.size() >= 12) return;
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    if (std::isalnum(static_cast<unsigned char>(character)) || character == ' ' || character == '-' || character == '_')
        pendingName_ += character;
}

bool FrontendMenu::saveProfiles(std::string& error) const
{
    std::error_code filesystemError;
    const auto parent = profilePath_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) { error = "Could not create local profile directory: " + filesystemError.message(); return false; }
    std::ofstream output(profilePath_, std::ios::trunc);
    if (!output) { error = "Could not write local profiles: " + profilePath_.string(); return false; }
    for (const auto& profile : profiles_) output << profile << '\n';
    output << "OPTIONS_V1\n";
    for (const auto& options : options_) {
        for (const auto value : options.values) output << value << ' ';
        for (const auto scheme : options.controllerSchemes) output << scheme << ' ';
        output << '\n';
    }
    if (!output) { error = "Local profile write ended early."; return false; }
    return true;
}

void FrontendMenu::accept()
{
    if (screen_ == Screen::profileSelect) {
        if (profiles_[profileSelection_.index()].empty()) {
            pendingName_.clear();
            screen_ = Screen::nameEntry;
        } else {
            screen_ = Screen::mainMenu;
        }
        return;
    }
    if (screen_ == Screen::nameEntry) {
        while (!pendingName_.empty() && pendingName_.back() == ' ') pendingName_.pop_back();
        if (pendingName_.empty()) { status_ = "ENTER A PROFILE NAME"; return; }
        profiles_[profileSelection_.index()] = pendingName_;
        std::string error;
        if (!saveProfiles(error)) { status_ = error; return; }
        status_.clear();
        screen_ = Screen::mainMenu;
        return;
    }
    if (screen_ == Screen::deleteProfile) {
        if (submenuSelection_.index() == 0) {
            const auto previous = profiles_[profileSelection_.index()];
            const auto previousOptions = options();
            profiles_[profileSelection_.index()].clear();
            options_[profileSelection_.index()] = Options{};
            std::string error;
            if (!saveProfiles(error)) {
                profiles_[profileSelection_.index()] = previous;
                options_[profileSelection_.index()] = previousOptions;
                status_ = error;
                return;
            }
        }
        back();
        return;
    }
    if (screen_ == Screen::mainMenu) {
        constexpr Screen destinations[]{Screen::missionSelect, Screen::twoPlayer, Screen::systemOptions, Screen::dossier};
        if (mainSelection_.index() == 1 && !hasSecondController()) {
            status_ = "CONNECT A SECOND CONTROLLER";
            return;
        }
        open(destinations[mainSelection_.index()]);
        return;
    }
    if (screen_ == Screen::missionSelect) {
        const auto selectedMission = submenuSelection_.index();
        if (!missionAccess(selectedMission, missionProgress_[profileSelection_.index()]).open) {
            status_.clear();
            return;
        }
        mission_ = selectedMission;
        open(Screen::briefing);
    } else if (screen_ == Screen::briefing) {
        startMissionLoad();
    } else if (screen_ == Screen::missionLoading) {
        return;
    } else if (screen_ == Screen::levelPreview) {
        return;
    } else if (screen_ == Screen::twoPlayer) {
        status_ = "MISSION LOCKED - COMPLETE ALL MISSION OBJECTIVES";
    } else if (screen_ == Screen::settings) {
        std::string error;
        if (!saveProfiles(error)) { status_ = error; return; }
        back();
    } else if (screen_ == Screen::controller1Options || screen_ == Screen::controller2Options) {
        const auto player = screen_ == Screen::controller1Options ? 0 : 1;
        const auto previous = options_[profileSelection_.index()].controllerSchemes[player];
        options_[profileSelection_.index()].controllerSchemes[player] = submenuSelection_.index();
        std::string error;
        if (!saveProfiles(error)) {
            options_[profileSelection_.index()].controllerSchemes[player] = previous;
            status_ = error; return;
        }
        back();
    } else if (screen_ == Screen::musicPlayer) {
        status_ = "MUSIC PLAYBACK NOT IMPLEMENTED";
    } else if (screen_ == Screen::moviePlayer) {
        status_ = submenuSelection_.index() < 2 ? "MOVIE PLAYBACK NOT IMPLEMENTED" : "MOVIE LOCKED";
    } else if (screen_ == Screen::cheatGrid) {
        status_ = "REWARD LOCKED";
    } else if (submenuSelection_.index() < menuDestinations_.size()) {
        open(menuDestinations_[submenuSelection_.index()]);
    }
}

void FrontendMenu::startMissionLoad()
{
    if (mission_ != 0) {
        status_ = "LOADING SUPPORT IS AVAILABLE FOR MISSION 1 ONLY";
        return;
    }

    cancelMissionLoad();
    {
        std::lock_guard lock(missionLoadMutex_);
        missionLoadStage_ = MissionLoadStage::staticArchive;
        missionLoadItem_.clear();
        missionLoadError_.clear();
        missionLoadCompletedBytes_ = 0;
        missionLoadTotalBytes_ = 0;
        missionLoadFinished_ = false;
        missionLoadSucceeded_ = false;
        missionTrack_.reset();
        missionTrackPreviewTexture_.reset();
        missionTrackPreviewStats_ = {};
    }
    open(Screen::missionLoading);
    const auto extractedRoot = missionLoadRoot_.empty() ? root_ : missionLoadRoot_;
    missionLoadWorker_ = std::jthread([this, extractedRoot](const std::stop_token stop) {
        std::string error;
        bool loaded = inspectProvingGroundsPackage(extractedRoot, stop,
            [this, stop](const MissionLoadProgress& progress) {
                if (stop.stop_requested()) return false;
                const auto capturePause = missionLoadCapturePause_;
                {
                    std::lock_guard lock(missionLoadMutex_);
                    missionLoadStage_ = progress.stage;
                    missionLoadItem_.assign(progress.item);
                    missionLoadCompletedBytes_ = progress.completedBytes;
                    missionLoadTotalBytes_ = progress.totalBytes;
                }
                if (capturePause.count() > 0) std::this_thread::sleep_for(capturePause);
                return !stop.stop_requested();
            }, error);
        runtime::TrackGeometryImport track;
        if (loaded) {
            {
                std::lock_guard lock(missionLoadMutex_);
                missionLoadStage_ = MissionLoadStage::trackGeometry;
                missionLoadItem_ = "new_provinggrounds.trk / leaf 74 / batch 5";
            }
            loaded = runtime::importTrackBatch(extractedRoot / "PG1" / "PG1.PCK",
                "new_provinggrounds.trk", 74, 5, stop, track, error);
        }
        std::lock_guard lock(missionLoadMutex_);
        if (loaded) missionTrack_ = std::move(track);
        missionLoadSucceeded_ = loaded;
        missionLoadError_ = std::move(error);
        missionLoadFinished_ = true;
    });
}

void FrontendMenu::cancelMissionLoad()
{
    if (!missionLoadWorker_.joinable()) return;
    missionLoadWorker_.request_stop();
    missionLoadWorker_.join();
}

void FrontendMenu::drawMissionLoading() const
{
    missionLoadingBackground_.draw(renderer_, {0.0f, 0.0f, canvasWidth, canvasHeight});

    MissionLoadStage stage;
    std::string item;
    std::string error;
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    bool finished = false;
    bool succeeded = false;
    {
        std::lock_guard lock(missionLoadMutex_);
        stage = missionLoadStage_;
        item = missionLoadItem_;
        error = missionLoadError_;
        completed = missionLoadCompletedBytes_;
        total = missionLoadTotalBytes_;
        finished = missionLoadFinished_;
        succeeded = missionLoadSucceeded_;
    }

    const SDL_Color lightBlue = sourceColor(0x3e09374c, 0x3efd70a4, 0x3f4e978d, 0x3f800000);
    const SDL_Color blue = sourceColor(0x3e54d4d6, 0x3e64e4e6, 0x3edadadc, 0x3f800000);
    const SDL_Color orange = sourceColor(0x3ee9a027, 0x3ed78d50, 0, 0x3f666666);
    const SDL_Color white = sourceColor(0x3f4353f8, 0x3f49fbe7, 0x3f5a1cac, 0x3f666666);
    missionLoadingTitleFont_.drawText(renderer_, "LOADING", 415.0f, 412.0f, canvasHeight, 1.0f, lightBlue);
    missionLoadingFont_.drawText(renderer_, "LOCATION:", 10.0f, 420.0f, canvasHeight, 1.0f, blue);
    missionLoadingFont_.drawText(renderer_, "TEXAS, USA", 130.0f, 420.0f, canvasHeight, 1.0f, white);
    missionLoadingFont_.drawText(renderer_, "TIME:", 10.0f, 408.0f, canvasHeight, 1.0f, blue);
    missionLoadingFont_.drawText(renderer_, " 8:00", 70.0f, 408.0f, canvasHeight, 1.0f, white);

    missionLoadingFont_.drawText(renderer_, "IES INTELLIGENCE:", 78.0f, 303.0f, canvasHeight, 1.0f, orange);
    constexpr std::array<std::string_view, 9> loadingCopy{
        "MUST COMPLETE PRIMARY OBJECTIVE ", "TO PASS LEVEL, AND SECONDARY ",
        "OBJECTIVES TO ADVANCE MISSIONS.", "", "",
        "DOUBLE TAP X BUTTON FOR TURBO BOOST.", "L1 BUTTON CYCLES OFFENSIVE WEAPONS.",
        "R1 BUTTON FIRES ACTIVE WEAPON.", "CIRCLE BUTTON FIRES GPS TRACKERS."
    };
    for (std::size_t index = 0; index < loadingCopy.size(); ++index) {
        const float baseline = 278.0f - static_cast<float>(index) * 18.0f;
        const SDL_Color copyColor = index >= 3 ? white : orange;
        missionLoadingFont_.drawText(renderer_, loadingCopy[index], 103.0f, baseline, canvasHeight, 1.0f,
                                     copyColor);
        if (index == 2) missionLoadingFont_.drawText(renderer_, "TIPS:", 78.0f, 208.0f,
                                                     canvasHeight, 1.0f, orange);
    }

    const float amount = total == 0 ? 0.0f : static_cast<float>(
        (std::min)(static_cast<double>(completed) / static_cast<double>(total), 1.0));
    // vkSprite stores source RGBA as GS 7-bit channels (color*128). SDL's
    // source-over path otherwise applies the texture's already decoded alpha
    // against the tint's source alpha a second time and makes the bar nearly
    // black. Preserve texture alpha while converting the GS vertex color to
    // modulation and let SDL apply the texture alpha once.
    const auto barTint = [](const SDL_Color color) {
        const auto toByte = [](const Uint8 value) {
            return static_cast<Uint8>((std::min)(255, (static_cast<int>(value) * 255 + 64) / 128));
        };
        return SDL_Color{toByte(color.r), toByte(color.g), toByte(color.b), 255};
    };
    const auto drawBarPart = [this, &barTint](const float x, const float width, const SDL_Color tint,
                                    const float u0, const float v0, const float u1, const float v1,
                                    const bool mirrorHorizontal = false) {
        const SDL_FRect source{(std::min)(u0, u1) / 16.0f, (std::min)(v0, v1) / 16.0f,
                               std::abs(u1 - u0) / 16.0f, std::abs(v1 - v0) / 16.0f};
        missionLoadingBar_.drawTintedMirrored(renderer_, {x, 405.0f, width, 25.0f}, barTint(tint),
            source, mirrorHorizontal || u1 < u0, v1 < v0);
    };
    drawBarPart(200.0f, 128.0f, blue, 0.0f, 512.0f, 4096.0f, 0.0f);
    drawBarPart(327.0f, 128.0f, blue, 0.0f, 512.0f, 4096.0f, 0.0f, true);
    drawBarPart(200.0f, 30.0f, lightBlue, 0.0f, 512.0f, 1000.0f, 0.0f);
    const float fillWidth = 216.0f * amount;
    if (fillWidth > 0.0f)
        drawBarPart(230.0f, fillWidth, lightBlue, 1000.0f, 512.0f,
                    1000.0f + 16.0f * fillWidth, 0.0f);
    if (amount > 0.99f)
        drawBarPart(443.0f, 10.0f, lightBlue, 0.0f, 512.0f, 300.0f, 0.0f, true);
    if (!finished) {
        const auto percent = static_cast<int>(amount * 100.0f);
        const auto percentText = "HOST READ " + std::to_string(percent) + "%";
        missionLoadingFont_.drawText(renderer_, percentText, 232.0f, 43.0f, canvasHeight, 1.0f, white);
    }
}

bool FrontendMenu::prepareMissionLevelPreview(std::string& error)
{
    if (!missionTrack_) { error = "Track geometry is unavailable for the depth view."; return false; }
    runtime::TrackPreviewRenderer previewRenderer;
    std::vector<std::uint8_t> pixels;
    if (!previewRenderer.render(missionTrack_->selected, static_cast<std::uint32_t>(canvasWidth),
                                static_cast<std::uint32_t>(canvasHeight), pixels,
                                missionTrackPreviewStats_, error)) return false;
    std::unique_ptr<SDL_Texture, SdlTextureDeleter> texture(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight)));
    if (!texture) { error = "Could not create level preview texture: " + std::string(SDL_GetError()); return false; }
    if (!SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), static_cast<int>(canvasWidth) * 4) ||
        !SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST) ||
        !SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE)) {
        error = "Could not upload the software depth image: " + std::string(SDL_GetError());
        return false;
    }
    missionTrackPreviewTexture_ = std::move(texture);
    return true;
}

void FrontendMenu::drawMissionLevelPreview() const
{
    SDL_SetRenderDrawColor(renderer_, 4, 8, 13, 255);
    SDL_RenderClear(renderer_);
    if (missionTrackPreviewTexture_)
        SDL_RenderTexture(renderer_, missionTrackPreviewTexture_.get(), nullptr, nullptr);
    constexpr SDL_Color labelColor{160, 189, 216, 255};
    font_.drawText(renderer_, "MISSION 1 TRACK INSPECTION - LEAF 74 / BATCH 5", 18, 24, canvasHeight, 0.72f, labelColor);
    font_.drawText(renderer_, "SOFTWARE PERSPECTIVE / DEPTH | ORIGINAL MATERIAL NOT IMPLEMENTED",
                   18, 8, canvasHeight, 0.58f, labelColor);
}

void FrontendMenu::configure(Screen destination, std::size_t selection)
{
    screen_ = destination;
    horizontalAxis_.reset();
    verticalAxis_.reset();
    menuItems_.clear();
    menuDestinations_.clear();
    menuTitle_.clear();
    if (destination == Screen::missionSelect) {
        missionSceneSlots_.fill(0);
        missionSceneChangedAt_.fill(static_cast<double>(SDL_GetTicks()) / 1000.0);
        missionCardPositions_.fill({60, 120});
        missionCardLastUpdatedAtMs_ = SDL_GetTicks();
    }
    if (const auto node = menuNode(screen_)) {
        menuTitle_ = node->title;
        menuItems_.assign(node->labels.begin(), node->labels.end());
        // ShMenu5 left/right (0x180868/0x180988): missions 1/7 skip Enemies.
        if (destination == Screen::briefing && (mission_ == 0 || mission_ == 6)) menuItems_.resize(2);
        menuDestinations_.assign(node->destinations.begin(), node->destinations.end());
    }
    submenuSelection_ = ui::MenuSelection(menuItems_.size());
    submenuSelection_.select(selection);
    status_.clear();
}

void FrontendMenu::open(Screen destination)
{
    menuStack_.push_back(screen_);
    menuSelectionStack_.push_back(submenuSelection_.index());
    configure(destination);
    if (destination == Screen::settings) entryOptions_ = options();
    if (destination == Screen::deleteProfile) submenuSelection_.select(1); // Safe default: No.
    if (destination == Screen::controller1Options || destination == Screen::controller2Options)
        submenuSelection_.select(options().controllerSchemes[destination == Screen::controller1Options ? 0 : 1]);
}

void FrontendMenu::back()
{
    if (screen_ == Screen::missionLoading) cancelMissionLoad();
    status_.clear();
    if (!menuStack_.empty()) {
        const auto destination = menuStack_.back();
        const auto selection = menuSelectionStack_.back();
        menuStack_.pop_back();
        menuSelectionStack_.pop_back();
        configure(destination, selection);
    } else if (screen_ == Screen::mainMenu || screen_ == Screen::nameEntry) {
        pendingName_.clear();
        configure(Screen::profileSelect);
    }
}

void FrontendMenu::horizontal(int direction)
{
    if (screen_ == Screen::settings) {
        const auto row = submenuSelection_.index();
        auto& value = options_[profileSelection_.index()].values[row];
        if (row == 0 || row == 4) {
            const int optionCount = row == 0 ? static_cast<int>(soundModeLabels.size()) : 2;
            value = (value + direction + optionCount) % optionCount;
        } else {
            value = std::clamp(value + direction, 0, 10);
        }
        status_.clear();
    } else if (screen_ == Screen::briefing) {
        submenuSelection_.move(direction);
    } else if (screen_ == Screen::cheatGrid) {
        submenuSelection_.move(direction);
        status_.clear();
    } else if (screen_ == Screen::missionSelect) {
        // ShMenu1::left/right (0x0016FA30 / 0x0016FA38) are empty.
    } else if (isCarousel(screen_) || screen_ == Screen::controller1Options ||
               screen_ == Screen::controller2Options) {
        moveSelection(direction);
    }
}

void FrontendMenu::secondary()
{
    if (screen_ == Screen::profileSelect && !profiles_[profileSelection_.index()].empty())
        open(Screen::deleteProfile);
    else if (screen_ == Screen::musicPlayer) {
        musicLayout_ = (musicLayout_ + 1) % 5;
        status_ = "LAYOUT " + std::to_string(musicLayout_ + 1) + " - VISUALIZER NOT IMPLEMENTED";
    } else if (screen_ == Screen::briefing && submenuSelection_.index() != 0)
        status_ = "MODEL PREVIEW NOT IMPLEMENTED";
}

void FrontendMenu::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        const auto which = event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION ? event.gaxis.which : event.gbutton.which;
        const int owner = screen_ == Screen::controller2Options ? 1 : 0;
        const bool wrongController = which != 0 && (count <= owner || ids[owner] != which);
        SDL_free(ids);
        if (wrongController) return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        const float value = static_cast<float>(event.gaxis.value) / 32767.0f;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) horizontalAxis_.setValue(value);
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) verticalAxis_.setValue(value);
        return;
    }
    if (isBack(event)) {
        if (screen_ == Screen::settings) options_[profileSelection_.index()] = entryOptions_;
        back(); return;
    }
    if (isAccept(event)) { accept(); return; }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) moveSelection(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveSelection(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) horizontal(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) horizontal(1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) secondary();
        else if (screen_ == Screen::nameEntry && event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
            if (!pendingName_.empty()) pendingName_.pop_back();
        }
        return;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    if (event.key.key == SDLK_UP) moveSelection(-1);
    else if (event.key.key == SDLK_DOWN) moveSelection(1);
    else if (event.key.key == SDLK_LEFT) horizontal(-1);
    else if (event.key.key == SDLK_RIGHT) horizontal(1);
    else if (screen_ != Screen::nameEntry && event.key.key == SDLK_S) secondary();
    else if (screen_ == Screen::nameEntry && event.key.key == SDLK_BACKSPACE) {
        if (!pendingName_.empty()) pendingName_.pop_back();
    } else if (screen_ == Screen::nameEntry && event.key.key == SDLK_SPACE) appendNameCharacter(' ');
    else if (screen_ == Screen::nameEntry && event.key.key >= SDLK_A && event.key.key <= SDLK_Z)
        appendNameCharacter(static_cast<char>('A' + event.key.key - SDLK_A));
    else if (screen_ == Screen::nameEntry && event.key.key >= SDLK_0 && event.key.key <= SDLK_9)
        appendNameCharacter(static_cast<char>('0' + event.key.key - SDLK_0));
}

void FrontendMenu::drawCentered(std::string_view text, const float yFromBottom, const float scale) const
{
    font_.drawText(renderer_, text, (canvasWidth - font_.textAdvance(text, scale)) * 0.5f,
                   yFromBottom, canvasHeight, scale);
}

void FrontendMenu::render() const
{
    SDL_SetRenderDrawColor(renderer_, 3, 13, 35, 255);
    SDL_RenderClear(renderer_);
    const SDL_FRect viewport{0, 0, canvasWidth, canvasHeight};
    if (screen_ == Screen::missionLoading) {
        drawMissionLoading();
        return;
    }
    if (screen_ == Screen::levelPreview) {
        drawMissionLevelPreview();
        return;
    }
    if (backgroundLoaded_) background_.render(renderer_, viewport);

    if (screen_ == Screen::missionSelect) {
        const auto selected = submenuSelection_.index();
        constexpr auto lightBlue = missionTextColor(17, 63, 103, 128);
        constexpr auto blue = missionTextColor(26, 28, 54, 128);
        constexpr auto white = missionTextColor(97, 100, 109, 115);
        constexpr auto orange = missionTextColor(58, 53, 0, 115);
        constexpr auto red = missionTextColor(49, 33, 0, 115);
        constexpr auto buttonTextOn = missionTextColor(51, 70, 128, 115);
        missionMask_.draw(renderer_, {0, 91, 640, 269}, [&] {
            ui::drawFilledPanel(renderer_, {0, 91, canvasWidth, 269}, {0, 0, 0, 178});
        });
        menuHighlightbar_.draw(renderer_, 0, 87, canvasWidth, 8);
        menuHighlightbar_.draw(renderer_, 0, 354, canvasWidth, 8);
        font_.drawText(renderer_, "AGENT", 10, 421, canvasHeight, 1.0f, blue);
        font_.drawText(renderer_, profiles_[profileSelection_.index()], 100, 421, canvasHeight, 1.0f, white);
        font_.drawText(renderer_, "OBJECTIVES", 10, 408, canvasHeight, 1.0f, blue);
        font_.drawText(renderer_, std::to_string(missionProgress_[profileSelection_.index()].objectivePoints),
                       162, 408, canvasHeight, 1.0f, white);
        profileFont_.drawText(renderer_, "MISSION SELECT", 420, 415, canvasHeight, 1.0f, lightBlue);

        constexpr std::array<int, 5> cardOffsets{-1, 0, 1, 2, 3};
        for (std::size_t slot = 0; slot < cardOffsets.size(); ++slot) {
            const auto mission = static_cast<std::size_t>((static_cast<int>(selected) + cardOffsets[slot] +
                static_cast<int>(missionScenes_.size())) % static_cast<int>(missionScenes_.size()));
            const auto position = missionCardPositions_[mission];
            const SDL_FRect card{static_cast<float>(position.x), static_cast<float>(position.y), 128, 64};
            const auto frameSlot = missionSceneSlots_[mission];
            const auto textureFrame = frameSlot < 3 ? frameSlot : frameSlot - 3;
            const SDL_Rect cardBounds{position.x, position.y, 128, 64};
            const SDL_Rect selectorBounds{57, 90, 526, 269};
            SDL_Rect cardClip{};
            if (!SDL_GetRectIntersection(&cardBounds, &selectorBounds, &cardClip)) continue;
            missionMask_.draw(renderer_, cardClip, [&] {
                missionScenes_[mission][textureFrame].draw(renderer_, card.x, card.y, card.w, card.h);
                if (missionPointThresholds[mission] > 0) {
                    const auto badge = static_cast<std::size_t>(std::find(missionPointThresholds.begin()+1,
                        missionPointThresholds.end(), missionPointThresholds[mission]) - missionPointThresholds.begin() - 1);
                    const auto* overlay = (slot % 3 == 0 ? &missionOverlay4_ : slot % 3 == 1 ? &missionOverlay2_ : &missionOverlay3_);
                    overlay->draw(renderer_, card.x, card.y, 128, 64);
                    if (badge < missionAccessLabels_.size())
                        missionAccessLabels_[badge].draw(renderer_, card.x, card.y, 128, 64);
                } else {
                    missionBlue_.draw(renderer_, card.x, card.y, 128, 64);
                }
            });
        }
        menuLightbar_.drawTinted(renderer_, {200, 88, 256, 32}, lightBlue);
        font_.drawText(renderer_, "MISSION", 285, 339, canvasHeight, 1.0f, buttonTextOn);
        char missionNumber[3]{};
        std::snprintf(missionNumber, sizeof(missionNumber), "%02d", static_cast<int>(selected + 1));
        // EuroStile12 is a temporary metric substitute for the unavailable
        // mission-number VKF. Match the reference glyph bounds while keeping
        // the recovered source anchor; source 1.12x1.04 scales describe the
        // dedicated font and do not fit this substitute's much smaller glyphs.
        constexpr SDL_Color missionNumberYellow{229, 200, 0, 255};
        profileFont_.drawTextScaled(renderer_, missionNumber, 197, 280, canvasHeight, 2.9f, 2.5f,
                                    missionNumberYellow);
        const auto missionTitle = catalogue::missions[selected];
        const auto split = missionTitle.rfind(' ');
        const auto title1 = split == std::string_view::npos ? missionTitle : missionTitle.substr(0, split);
        const auto title2 = split == std::string_view::npos ? std::string_view{} : missionTitle.substr(split + 1);
        // The source title uses the dedicated menu_mission_name VKF, which is
        // not decoded yet. These scales compensate for EuroStile12's smaller
        // substitute metrics; preserve the independently recovered baselines.
        profileFont_.drawTextScaled(renderer_, title1, 301, 299, canvasHeight, 1.5f, 1.15f, red);
        if (!title2.empty()) profileFont_.drawTextScaled(renderer_, title2, 301, 281, canvasHeight, 1.5f, 1.15f, red);
        if (selected == 0) {
            constexpr std::array<std::string_view, 5> desc{"COMPLETE ONE LAP AROUND", "THE TEST TRACK AT THE IES",
                "TRAINING FACILITY TO EARN", "SPYHUNTER STATUS AND", "BEGIN OFFICIAL FIELD WORK."};
            constexpr std::array<float, 5> baselines{236,216,196,176,156};
            for (std::size_t i=0;i<desc.size();++i) font_.drawText(renderer_, desc[i], 200, baselines[i], canvasHeight, 1.0f, white);
        } else {
            const auto access = missionAccess(selected, missionProgress_[profileSelection_.index()]);
            if (access.insufficientPoints) {
                font_.drawText(renderer_, "NOT ENOUGH OBJECTIVE POINTS", 200, 236, canvasHeight, 1.0f, white);
                font_.drawText(renderer_, "TO ACCESS LEVEL.", 200, 216, canvasHeight, 1.0f, white);
            }
            if (access.previousPrimaryIncomplete) {
                const float predecessorBaseline = access.insufficientPoints ? 186.0f : 236.0f;
                font_.drawText(renderer_, catalogue::missions[selected - 1], 200, predecessorBaseline, canvasHeight, 1.0f, white);
                font_.drawText(renderer_, "PRIMARY OBJECTIVE NOT", 200, predecessorBaseline - 20, canvasHeight, 1.0f, white);
                font_.drawText(renderer_, "COMPLETE.", 200, predecessorBaseline - 40, canvasHeight, 1.0f, white);
            }
        }
        rendezvousLine_.drawTinted(renderer_, {121, 294, static_cast<float>(rendezvousLine_.width()),
                                               static_cast<float>(rendezvousLine_.height())}, orange);
        profileFont_.drawText(renderer_, "RENDEZVOUS", 212, 114, canvasHeight, 1.0f, white);
        char allowance[16]{};
        const auto seconds = missionAllowance(selected,
            missionProgress_[profileSelection_.index()].allLevelsOverride);
        std::snprintf(allowance, sizeof(allowance), "%2d:%02d", seconds/60, seconds%60);
        profileFont_.drawTextScaled(renderer_, allowance, 380, 113, canvasHeight, 1.2f, 1.4f, orange);
        profileFont_.drawText(renderer_, "MIN", 478, 115, canvasHeight, 1.0f, orange);
        ui::drawFilledPanel(renderer_, {0, 407, canvasWidth, 41}, {0,0,0,255});
        navIcon_.draw(renderer_, 80, 408); acceptIcon_.draw(renderer_, 240, 408); backIcon_.draw(renderer_, 440, 408);
        font_.drawText(renderer_, "NAV", 120, 15, canvasHeight, 1, white);
        font_.drawText(renderer_, "ACCEPT", 280, 15, canvasHeight, 1, white);
        font_.drawText(renderer_, "BACK", 480, 15, canvasHeight, 1, white);
        return;
    }

    if (screen_ == Screen::profileSelect) {
        const SDL_Color blue{30, 145, 225, 255};
        const SDL_Color gray{182, 190, 201, 255};
        profileFont_.drawText(renderer_, "PROFILE MENU", 440.0f, 415.0f, canvasHeight, 1.0f, blue);
        // ShMenu's BlackBack spans y=91..360 in bottom-origin coordinates. The two
        // eight-pixel menu bars cap that dark table area at y=86 and y=353.
        ui::drawFilledPanel(renderer_, {0.0f, 88.0f, canvasWidth, 269.0f}, {0, 0, 0, 178});
        ui::drawFilledPanel(renderer_, {0.0f, 87.0f, canvasWidth, 8.0f}, {8, 66, 112, 225});
        ui::drawFilledPanel(renderer_, {0.0f, 354.0f, canvasWidth, 8.0f}, {8, 66, 112, 225});
        profileFont_.drawText(renderer_, "AGENT NAME", 60.0f, 340.0f, canvasHeight, 1.0f, blue);
        profileFont_.drawText(renderer_, "LEVEL", 290.0f, 340.0f, canvasHeight, 1.0f, blue);
        profileFont_.drawText(renderer_, "OBJECTIVES", 440.0f, 340.0f, canvasHeight, 1.0f, blue);
        for (std::size_t index = 0; index < profiles_.size(); ++index) {
            const float y = 311.0f - static_cast<float>(index) * 35.0f;
            if (index == profileSelection_.index())
                profileLightbar_.drawTinted(renderer_, {-6.0f, 115.0f + static_cast<float>(index) * 35.0f, 640.0f, 32.0f},
                    sourceColor(0x3e09374c, 0x3efd70a4, 0x3f4e978d, 0x3f800000));
            const bool empty = profiles_[index].empty();
            const SDL_Color color = index == profileSelection_.index() ? SDL_Color{0, 0, 0, 255} : gray;
            profileFont_.drawText(renderer_, empty ? "--------" : profiles_[index], 87.0f, y, canvasHeight, 1.0f, color);
            profileFont_.drawText(renderer_, empty ? "----------" : "MISSION 1", 287.0f, y, canvasHeight, 1.0f, color);
            profileFont_.drawText(renderer_, empty ? "  --" : "  0", 467.0f, y, canvasHeight, 1.0f, color);
        }
        navIcon_.draw(renderer_, 30.0f, 408.0f);
        acceptIcon_.draw(renderer_, 160.0f, 408.0f);
        backIcon_.draw(renderer_, 490.0f, 408.0f);
        font_.drawText(renderer_, "NAV", 70.0f, 15.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "ACCEPT", 200.0f, 15.0f, canvasHeight, 1.0f, gray);
        if (!profiles_[profileSelection_.index()].empty()) {
            deleteIcon_.draw(renderer_, 336.0f, 408.0f);
            font_.drawText(renderer_, "DELETE", 380.0f, 15.0f, canvasHeight, 1.0f, gray);
        }
        font_.drawText(renderer_, "BACK", 530.0f, 15.0f, canvasHeight, 1.0f, gray);
    } else if (screen_ == Screen::nameEntry) {
        drawCentered("CREATE PROFILE", 360.0f);
        // Host name-entry layout; original virtual keyboard is not reconstructed yet.
        profileLightbar_.drawTinted(renderer_, {90.0f, 165.0f, 460.0f, 50.0f},
            sourceColor(0x3e09374c, 0x3efd70a4, 0x3f4e978d, 0x3f800000));
        drawCentered(pendingName_.empty() ? "_" : pendingName_ + "_", 240.0f);
        drawCentered("TYPE A NAME - BACKSPACE TO ERASE", 105.0f, 0.65f);
        drawCentered("X/ENTER  ACCEPT", 62.0f, 0.65f);
    } else if (screen_ == Screen::mainMenu) {
        const SDL_Color blue{50, 54, 105, 255};
        constexpr auto selected = sourceColor(0x3ecf0069, 0x3f0c8b44, 0x3f800000, 0x3f666666);
        constexpr auto unselected = sourceColor(0, 0x3d1ba5e3, 0x3e0d4fdf, 0x3f3f3b64);
        constexpr auto selectorTint = sourceColor(0x3e88a71e, 0x3ef97247, 0x3f800000, 0x3f400000);
        const SDL_Color gray{170, 176, 192, 255};
        const SDL_Color metadataWhite{170, 176, 192, 255};
        const SDL_Color titleBlue{32, 125, 205, 255};
        profileFont_.drawText(renderer_, "AGENT", 10.0f, 421.0f, canvasHeight, 0.8f, blue);
        profileFont_.drawText(renderer_, profiles_[profileSelection_.index()], 100.0f, 421.0f, canvasHeight, 0.8f, metadataWhite);
        profileFont_.drawText(renderer_, "OBJECTIVES", 10.0f, 408.0f, canvasHeight, 0.8f, blue);
        profileFont_.drawText(renderer_, "0", 162.0f, 408.0f, canvasHeight, 0.8f, metadataWhite);
        font_.drawText(renderer_, "MAIN MENU", 473.0f, 415.0f, canvasHeight, 1.0f, titleBlue);
        // Recovered ShMenu0 text anchors: (260,280), (250,240), (240,195), (280,150).
        constexpr std::array<SDL_FPoint, 4> anchors{{{260, 280}, {250, 240}, {240, 195}, {280, 150}}};
        for (std::size_t index = 0; index < mainItems.size(); ++index) {
            if (index == mainSelection_.index()) {
                menuLightbar_.drawTinted(renderer_, mainSelectorBounds[index], selectorTint);
            }
            font_.drawText(renderer_, mainItems[index], anchors[index].x, anchors[index].y,
                           canvasHeight, 1.0f, index == mainSelection_.index() ? selected : unselected);
        }
        // The reference has a black band below the lower blue ruler, not a blue
        // footer panel. The PS2 nav sprites/labels are drawn over this band.
        ui::drawFilledPanel(renderer_, {0, 407, canvasWidth, 41}, {0, 0, 0, 255});
        navIcon_.draw(renderer_, 80.0f, 408.0f);
        acceptIcon_.draw(renderer_, 240.0f, 408.0f);
        backIcon_.draw(renderer_, 440.0f, 408.0f);
        font_.drawText(renderer_, "NAV", 120.0f, 15.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "ACCEPT", 280.0f, 15.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "BACK", 480.0f, 15.0f, canvasHeight, 1.0f, gray);
    } else if (menuNode(screen_)) {
        const SDL_Color blue{50, 54, 105, 255};
        const SDL_Color titleBlue{32, 125, 205, 255};
        constexpr auto selected = sourceColor(0x3ecf0069, 0x3f0c8b44, 0x3f800000, 0x3f666666);
        const bool controllerOptions = screen_ == Screen::controller1Options || screen_ == Screen::controller2Options;
        // ShMenu8/14 override ShButton's unselected text with unSelectedColor.
        const auto unselected = controllerOptions
            ? sourceColor(0x3deb50b1, 0x3e28db8c, 0x3e9a9fbe, 0x3f666666)
            : sourceColor(0, 0x3d1ba5e3, 0x3e0d4fdf, 0x3f3f3b64);
        const SDL_Color gray{170, 176, 192, 255};
        const bool settings = screen_ == Screen::settings;
        if (settings || screen_ == Screen::dossier) {
            // ShMenu::draw renders its 70%-alpha black sprite over this part of MBACK.
            // The source sprite is positioned at (0, 91) with size 640 x 269 in
            // bottom-origin coordinates.
            ui::drawFilledPanel(renderer_, {0.0f, 88.0f, canvasWidth, 269.0f}, {0, 0, 0, 178});
        }
        profileFont_.drawText(renderer_, "AGENT", 10.0f, 421.0f, canvasHeight, 0.8f, blue);
        profileFont_.drawText(renderer_, profiles_[profileSelection_.index()], 100.0f, 421.0f, canvasHeight, 0.8f, gray);
        profileFont_.drawText(renderer_, "OBJECTIVES", 10.0f, 408.0f, canvasHeight, 0.8f, blue);
        profileFont_.drawText(renderer_, "0", 162.0f, 408.0f, canvasHeight, 0.8f, gray);
        if (settings || screen_ == Screen::dossier) {
            // ShMenu::draw overlays the shared 512x8 ruler at source sprite
            // positions y=353 and y=86, each stretched to the 640px canvas.
            menuHighlightbar_.draw(renderer_, 0.0f, 87.0f, canvasWidth, 8.0f);
            menuHighlightbar_.draw(renderer_, 0.0f, 354.0f, canvasWidth, 8.0f);
        }
        if (settings) {
            // ShMenu6 uses the shared Euro12Font title object, at the source draw
            // anchor (500, 415), tinted with MSetupState::mColorLightBlue.
            profileFont_.drawText(renderer_, menuTitle_, 500.0f, 415.0f, canvasHeight, 1.0f,
                                  sourceColor(0x3e09374c, 0x3efd70a4, 0x3f4e978d, 0x3f800000));
        } else if (screen_ == Screen::dossier) {
            profileFont_.drawText(renderer_, menuTitle_, 510.0f, 415.0f, canvasHeight, 1.0f,
                                  sourceColor(0x3e09374c, 0x3efd70a4, 0x3f4e978d, 0x3f800000));
        } else {
            const float titleScale = 0.9f;
            font_.drawText(renderer_, menuTitle_, canvasWidth - 15.0f - font_.textAdvance(menuTitle_, titleScale),
                           415.0f, canvasHeight, titleScale, titleBlue);
        }
        const bool carousel = isCarousel(screen_);
        if (screen_ == Screen::dossier) {
            // ShMenu4 draw uses runtime-tweakable table globals. These anchors are
            // measured from the supplied 640x480 GS frame after applying its
            // 480/448 vertical output scale; the source globals aren't stored with
            // these values in the extracted ELF.
            constexpr float headingBaseline = dossierFirstRowBaseline + 20.0f;
            constexpr auto dossierBlue = sourceColor(0x3e54d4d6, 0x3e64e4e6, 0x3edadadc, 0x3f800000);
            constexpr auto dossierWhite = sourceColor(0x3f4353f8, 0x3f49fbe7, 0x3f5a1cac, 0x3f666666);
            profileFont_.drawText(renderer_, "MISSION", dossierTableX - 5.0f, headingBaseline,
                                  canvasHeight, 1.0f, dossierWhite);
            profileFont_.drawText(renderer_, "TIME", dossierTableX + dossierTimeColumnStep - 5.0f,
                                  headingBaseline, canvasHeight, 1.0f, dossierWhite);
            profileFont_.drawText(renderer_, "OBJECTIVES", dossierTableX + dossierObjectivesColumnStep,
                                  headingBaseline, canvasHeight, 1.0f, dossierWhite);
            for (std::size_t index = 0; index < catalogue::missions.size(); ++index) {
                const float row = static_cast<float>(index);
                const float baseline = dossierFirstRowBaseline - row * dossierRowStep;
                const auto color = index < dossierOpenMissionCount_ ? dossierWhite : dossierBlue;
                font_.drawText(renderer_, catalogue::missions[index], dossierTableX, baseline,
                               canvasHeight, 1.0f, color);
                char timeText[16]{};
                const auto elapsed = dossierElapsedSeconds_[index];
                std::snprintf(timeText, sizeof(timeText), "%2d:%02d", elapsed / 60, elapsed % 60);
                font_.drawText(renderer_, timeText, dossierTableX + dossierTimeColumnStep,
                               baseline, canvasHeight, 1.0f, color);

                for (int objective = 0; objective < dossierObjectiveCounts[index]; ++objective) {
                    const float x = dossierTableX + dossierObjectivesColumnStep + 30.0f * objective;
                    const float y = 428.0f - baseline;
                    // The retail 32x32 box textures include transparent margins;
                    // drawing their native size produces the small square outline.
                    if (dossierObjectives_[index][static_cast<std::size_t>(objective)]) {
                        constexpr auto completedTint = sourceColor(0x3ee9a027, 0x3ed78d50, 0, 0x3f666666);
                        dossierCheckedBox_.drawTinted(renderer_, {x, y, 32.0f, 32.0f}, completedTint);
                    }
                    dossierUncheckedBox_.draw(renderer_, x, y);
                }
            }
        }
        if (carousel && screen_ != Screen::cheatGrid) {
            drawCentered(std::to_string(submenuSelection_.index() + 1) + " / " + std::to_string(menuItems_.size()),
                         310.0f, 0.8f);
            std::string_view detail;
            if (screen_ == Screen::missionSelect)
                detail = missionAccess(submenuSelection_.index(), missionProgress_[profileSelection_.index()]).open
                    ? "MISSION OPEN" : "MISSION LOCKED";
            else if (screen_ == Screen::twoPlayer) detail = "MISSION LOCKED";
            else if (screen_ == Screen::musicPlayer) detail = catalogue::musicAssets[submenuSelection_.index()];
            else if (screen_ == Screen::moviePlayer)
                detail = submenuSelection_.index() < 2 ? "AVAILABLE" : "LOCKED";
            else detail = catalogue::rewardRequirements[submenuSelection_.index()];
            drawCentered(detail, 170, 0.7f);
            if (screen_ == Screen::missionSelect) {
                const auto selectedMission = submenuSelection_.index();
                const auto access = missionAccess(selectedMission, missionProgress_[profileSelection_.index()]);
                if (access.insufficientPoints) {
                    drawCentered("NOT ENOUGH OBJECTIVE POINTS", 139, 0.62f);
                    drawCentered("TO ACCESS LEVEL.", 123, 0.62f);
                }
                if (access.previousPrimaryIncomplete) {
                    const float reasonTop = access.insufficientPoints ? 99.0f : 139.0f;
                    drawCentered(catalogue::missions[selectedMission - 1], reasonTop, 0.58f);
                    drawCentered("PRIMARY OBJECTIVE NOT", reasonTop - 16.0f, 0.58f);
                    drawCentered("COMPLETE.", reasonTop - 32.0f, 0.58f);
                }
            } else {
                drawCentered("LEFT / RIGHT  SELECT", 110, 0.65f);
            }
            if (screen_ == Screen::musicPlayer) drawCentered("SQUARE / S  CHANGE LAYOUT", 85, 0.6f);
        }
        if (screen_ == Screen::cheatGrid) {
            // Host 4x4 presentation; original grid geometry/directional rules
            // remain unverified. No local profile has unlocked rewards yet.
            constexpr SDL_FRect content{48, 4, 160, 24};
            for (std::size_t index = 0; index < catalogue::rewards.size(); ++index) {
                const float x = 65 + 135.0f * static_cast<float>(index % 4);
                const float y = 120 + 42.0f * static_cast<float>(index / 4);
                if (index == submenuSelection_.index())
                    menuLightbar_.drawTinted(renderer_, {x, y, 110, 27},
                        sourceColor(0x3e88a71e, 0x3ef97247, 0x3f800000, 0x3f400000), &content);
                profileFont_.drawText(renderer_, std::to_string(index + 1) + " LOCKED", x + 8,
                    canvasHeight - y - 18, canvasHeight, 0.7f, gray);
            }
            const auto reward = catalogue::rewards[submenuSelection_.index()];
            drawCentered(reward, 135, std::min(0.8f, 530.0f / font_.textAdvance(reward, 1.0f)));
            drawCentered(catalogue::rewardRequirements[submenuSelection_.index()], 105, 0.65f);
        }
        if (screen_ == Screen::briefing) {
            drawCentered(catalogue::missions[mission_], 330, 0.8f);
            drawCentered("MISSION CONTENT NOT IMPLEMENTED", 145, 0.65f);
            drawCentered("CROSS / ENTER  START", 105, 0.7f);
        }
        if (screen_ == Screen::deleteProfile) drawCentered(profiles_[profileSelection_.index()], 330, 0.9f);
        constexpr float itemStep = 40.0f;
        for (std::size_t index = 0; index < menuItems_.size(); ++index) {
            if (screen_ == Screen::cheatGrid) break;
            if (carousel && index != submenuSelection_.index()) continue;
            const float labelScale = carousel ? std::min(1.0f, 490.0f / std::max(1.0f, font_.textAdvance(menuItems_[index], 1.0f))) : 1.0f;
            SDL_FPoint anchor{240.0f, 280.0f - static_cast<float>(index) * itemStep};
            if (carousel) anchor = {(canvasWidth - font_.textAdvance(menuItems_[index], labelScale)) * 0.5f, 250};
            constexpr std::array<float, 5> settingsBaselines{310, 278, 246, 214, 182};
            if (screen_ == Screen::settings) anchor = {50, settingsBaselines[index]};
            SDL_FRect sprite{anchor.x - 25.0f, canvasHeight - anchor.y - 16.0f,
                font_.textAdvance(menuItems_[index], labelScale) + 50.0f, 22.0f};
            // Nonzero-alpha content bounds measured from the standard 256x32
            // lightbar. Placeholder screens fit this real art around their label.
            constexpr SDL_FRect content{48.0f, 4.0f, 160.0f, 24.0f};
            bool originalBounds = false;
            if (screen_ == Screen::systemOptions && index < 3) {
                constexpr SDL_FPoint text[]{{270,280},{255,240},{276,195}};
                constexpr SDL_FRect bounds[]{{215,148,206,30},{190,188,256,30},{230,234,170,30}};
                anchor = text[index]; sprite = bounds[index]; originalBounds = true;
            } else if (screen_ == Screen::extras && index < 3) {
                constexpr SDL_FPoint text[]{{248,280},{248,240},{260,195}};
                constexpr SDL_FRect bounds[]{{183,148,274,30},{176,188,290,30},{196,233,244,30}};
                anchor = text[index]; sprite = bounds[index]; originalBounds = true;
            } else if (controllerOptions && index < 3) {
                constexpr SDL_FPoint text[]{{100,339},{260,339},{420,339}};
                constexpr SDL_FRect bounds[]{{40,90,230,32},{201,90,220,32},{368,90,185,30}};
                anchor = text[index]; sprite = bounds[index]; originalBounds = true;
            } else if (screen_ == Screen::settings) {
                constexpr std::array<float, 5> selectorX{-10, -20, -30, -30, -20};
                constexpr std::array<float, 5> selectorWidths{220, 280, 350, 350, 280};
                // The GS reference places the selector art 6 logical pixels
                // above the row anchor used by the meter sprites.
                constexpr float settingsRowTop = 114.0f;
                constexpr float settingsRowStep = 32.0f;
                sprite = {selectorX[index], settingsRowTop + settingsRowStep * static_cast<float>(index),
                          selectorWidths[index], 35.0f};
                originalBounds = true;
            }
            if (index == submenuSelection_.index()) {
                menuLightbar_.drawTinted(renderer_, sprite,
                    sourceColor(0x3e88a71e, 0x3ef97247, 0x3f800000, 0x3f400000),
                    originalBounds ? nullptr : &content);
            }
            const bool selectedSettingsRow = index == submenuSelection_.index();
            if (screen_ == Screen::settings) {
                constexpr auto settingsOn = sourceColor(0x3ecf0069, 0x3f0c8b44, 0x3f800000, 0x3f666666);
                constexpr auto settingsOff = sourceColor(0x3deb50b1, 0x3e28db8c, 0x3e9a9fbe, 0x3f666666);
                profileFont_.drawText(renderer_, menuItems_[index], anchor.x, anchor.y, canvasHeight, 1.0f,
                                      selectedSettingsRow ? settingsOn : settingsOff);
            } else {
                font_.drawText(renderer_, menuItems_[index], anchor.x, anchor.y, canvasHeight, labelScale,
                               selectedSettingsRow ? selected : unselected);
            }
            if (screen_ == Screen::settings) {
                const auto value = options().values[index];
                const SDL_Color valueColor = selectedSettingsRow
                    ? sourceColor(0x3ecf0069, 0x3f0c8b44, 0x3f800000, 0x3f666666)
                    : sourceColor(0x3deb50b1, 0x3e28db8c, 0x3e9a9fbe, 0x3f666666);
                if (index == 0) {
                    profileFont_.drawText(renderer_, soundModeLabels[static_cast<std::size_t>(value)],
                                          270, anchor.y, canvasHeight, 1.0f, valueColor);
                } else if (index == 4) {
                    profileFont_.drawText(renderer_, value ? "ON" : "OFF", 270, anchor.y,
                                          canvasHeight, 1.0f, valueColor);
                } else {
                    constexpr float meterX = 270.0f;
                    constexpr float meterCellSize = 20.0f;
                    constexpr float meterPitch = 25.0f;
                    constexpr float settingsRowTop = 120.0f;
                    constexpr float settingsRowStep = 32.0f;
                    const float meterTop = settingsRowTop + settingsRowStep * static_cast<float>(index);
                    constexpr auto emptyTint = sourceColor(0x3e54d4d6, 0x3e64e4e6, 0x3edadadc, 0x3f800000);
                    constexpr auto filledTint = sourceColor(0x3ee9a027, 0x3ed78d50, 0x00000000, 0x3f666666);
                    for (int cell = 0; cell < 10; ++cell) {
                        const SDL_FRect cellRect{meterX + cell * meterPitch, meterTop,
                                                 meterCellSize, meterCellSize};
                        menuEmptyBox_.drawTinted(renderer_, cellRect, emptyTint);
                        if (cell < value)
                            menuFilledBox_.drawTinted(renderer_, cellRect, filledTint);
                    }
                }
            }
        }
        ui::drawFilledPanel(renderer_, {0, 407, canvasWidth, 41}, {0, 0, 0, 255});
        if (screen_ == Screen::dossier) {
            backIcon_.draw(renderer_, 440.0f, 408.0f);
            font_.drawText(renderer_, "BACK", 480.0f, 15.0f, canvasHeight, 1.0f, gray);
        } else {
            navIcon_.draw(renderer_, 80.0f, 408.0f);
            acceptIcon_.draw(renderer_, 240.0f, 408.0f);
            backIcon_.draw(renderer_, 440.0f, 408.0f);
            font_.drawText(renderer_, "NAV", 120.0f, 15.0f, canvasHeight, 1.0f, gray);
            font_.drawText(renderer_, "ACCEPT", 280.0f, 15.0f, canvasHeight, 1.0f, gray);
            font_.drawText(renderer_, "BACK", 480.0f, 15.0f, canvasHeight, 1.0f, gray);
        }
    }
    if (!status_.empty()) drawCentered(status_, 52.0f, 0.58f);
}
}
