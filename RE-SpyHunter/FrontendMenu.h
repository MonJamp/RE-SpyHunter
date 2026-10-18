#pragma once

#include "../GameRuntime/CutscenePlayer.h"
#include "../GameRuntime/PackedTexture.h"
#include "../GameRuntime/VkfTextureFont.h"
#include "../GameUI/MenuSelection.h"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace spyhunter::game
{
class FrontendMenu
{
public:
    static constexpr std::size_t profileCount = 3;
    enum class Screen { profileSelect, nameEntry, mainMenu, operations, twoPlayer, systemOptions, dossier,
                        missionSelect, rendezvous, settings, controller, extras, missionData,
                        musicPlayer, moviePlayer, cheatGrid, controller1Options, controller2Options };

    FrontendMenu(SDL_Renderer* renderer, const std::filesystem::path& root,
                 std::filesystem::path profilePath, std::filesystem::path backgroundPath);
    bool load(std::string& error);
    void handleEvent(const SDL_Event& event);
    void update();
    void render() const;

    Screen screen() const { return screen_; }
    std::size_t selectedProfile() const { return profileSelection_.index(); }
    std::size_t selectedMainItem() const { return mainSelection_.index(); }
    std::size_t selectedSubmenuItem() const { return submenuSelection_.index(); }
    const std::array<std::string, profileCount>& profiles() const { return profiles_; }
    const std::string& pendingName() const { return pendingName_; }

private:
    void moveSelection(int direction);
    void accept();
    void back();
    void appendNameCharacter(char character);
    bool saveProfiles(std::string& error) const;
    void drawCentered(std::string_view text, float yFromBottom, float scale = 1.0f) const;

    SDL_Renderer* renderer_;
    std::filesystem::path root_;
    std::filesystem::path profilePath_;
    std::filesystem::path backgroundPath_;
    runtime::VkfTextureFont font_;
    runtime::VkfTextureFont profileFont_;
    runtime::PackedTexture navIcon_;
    runtime::PackedTexture acceptIcon_;
    runtime::PackedTexture deleteIcon_;
    runtime::PackedTexture backIcon_;
    runtime::PackedTexture menuLightbar_;
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
};
}
