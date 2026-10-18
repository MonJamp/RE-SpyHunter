#pragma once

#include "../GameRuntime/CutscenePlayer.h"
#include "../GameRuntime/VkfTextureFont.h"
#include <array>
#include <filesystem>
#include <string>

namespace spyhunter::game
{
class FrontendMenu
{
public:
    static constexpr std::size_t profileCount = 4;
    enum class Screen { profileSelect, nameEntry, mainMenu };

    FrontendMenu(SDL_Renderer* renderer, const std::filesystem::path& root,
                 std::filesystem::path profilePath, std::filesystem::path backgroundPath);
    bool load(std::string& error);
    void handleEvent(const SDL_Event& event);
    void update();
    void render() const;

    Screen screen() const { return screen_; }
    std::size_t selectedProfile() const { return selectedProfile_; }
    std::size_t selectedMainItem() const { return selectedMainItem_; }
    const std::array<std::string, profileCount>& profiles() const { return profiles_; }
    const std::string& pendingName() const { return pendingName_; }

private:
    void moveSelection(int direction);
    void accept();
    void back();
    void appendNameCharacter(char character);
    bool saveProfiles(std::string& error) const;
    void drawCentered(std::string_view text, float yFromBottom, float scale = 1.0f) const;
    void drawHighlight(float x, float y, float width, float height) const;

    SDL_Renderer* renderer_;
    std::filesystem::path root_;
    std::filesystem::path profilePath_;
    std::filesystem::path backgroundPath_;
    runtime::VkfTextureFont font_;
    runtime::CutsceneSession background_;
    bool backgroundLoaded_ = false;
    std::array<std::string, profileCount> profiles_{};
    Screen screen_ = Screen::profileSelect;
    std::size_t selectedProfile_ = 0;
    std::size_t selectedMainItem_ = 0;
    std::string pendingName_;
    std::string status_;
};
}
