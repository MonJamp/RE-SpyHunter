#include "FrontendMenu.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace spyhunter::game
{
namespace
{
constexpr float canvasWidth = 640.0f;
constexpr float canvasHeight = 448.0f;
constexpr std::array<std::string_view, 4> mainItems{
    "OPERATIONS", "TWO PLAYER", "SYSTEM OPTIONS", "DOSSIER"
};

bool isAccept(const SDL_Event& event)
{
    return (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.key == SDLK_RETURN || event.key.key == SDLK_X)) ||
           (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH);
}
}

FrontendMenu::FrontendMenu(SDL_Renderer* renderer, const std::filesystem::path& root,
                           std::filesystem::path profilePath, std::filesystem::path backgroundPath)
    : renderer_(renderer), root_(root), profilePath_(std::move(profilePath)),
      backgroundPath_(std::move(backgroundPath)) {}

bool FrontendMenu::load(std::string& error)
{
    if (!font_.loadFromPackedArchive(root_ / "DATA" / "MENU.PCK", "Eurobet.vkf", renderer_, error))
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
    error.clear();
    return true;
}

void FrontendMenu::update()
{
    if (backgroundLoaded_) background_.update();
}

void FrontendMenu::moveSelection(const int direction)
{
    if (screen_ == Screen::profileSelect) {
        selectedProfile_ = (selectedProfile_ + profiles_.size() + direction) % profiles_.size();
    } else if (screen_ == Screen::mainMenu) {
        selectedMainItem_ = (selectedMainItem_ + mainItems.size() + direction) % mainItems.size();
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
    if (!output) { error = "Local profile write ended early."; return false; }
    return true;
}

void FrontendMenu::accept()
{
    if (screen_ == Screen::profileSelect) {
        if (profiles_[selectedProfile_].empty()) {
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
        profiles_[selectedProfile_] = pendingName_;
        std::string error;
        if (!saveProfiles(error)) { status_ = error; return; }
        status_.clear();
        screen_ = Screen::mainMenu;
        return;
    }
    status_ = std::string(mainItems[selectedMainItem_]) + " IS NOT YET AVAILABLE";
}

void FrontendMenu::handleEvent(const SDL_Event& event)
{
    if (isAccept(event)) { accept(); return; }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) moveSelection(-1);
        else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveSelection(1);
        else if (screen_ == Screen::nameEntry && event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
            if (!pendingName_.empty()) pendingName_.pop_back();
        }
        return;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    if (event.key.key == SDLK_UP) moveSelection(-1);
    else if (event.key.key == SDLK_DOWN) moveSelection(1);
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

void FrontendMenu::drawHighlight(const float x, const float y, const float width, const float height) const
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 70, 175, 220, 150);
    const SDL_FRect rectangle{x, y, width, height};
    SDL_RenderFillRect(renderer_, &rectangle);
}

void FrontendMenu::render() const
{
    SDL_SetRenderDrawColor(renderer_, 3, 13, 35, 255);
    SDL_RenderClear(renderer_);
    const SDL_FRect viewport{0, 0, canvasWidth, canvasHeight};
    if (backgroundLoaded_) background_.render(renderer_, viewport);

    if (screen_ == Screen::profileSelect) {
        const SDL_Color blue{30, 145, 225, 255};
        const SDL_Color gray{180, 190, 200, 255};
        font_.drawText(renderer_, "PROFILE MENU", 440.0f, 398.0f, canvasHeight, 1.0f, blue);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 8, 66, 112, 220);
        const SDL_FRect topRail{0, 76, canvasWidth, 48};
        const SDL_FRect bottomRail{0, 390, canvasWidth, 52};
        SDL_RenderFillRect(renderer_, &topRail);
        SDL_RenderFillRect(renderer_, &bottomRail);
        font_.drawText(renderer_, "AGENT NAME", 60.0f, 345.0f, canvasHeight, 0.9f, blue);
        font_.drawText(renderer_, "LEVEL", 290.0f, 345.0f, canvasHeight, 0.9f, blue);
        font_.drawText(renderer_, "OBJECTIVES", 440.0f, 345.0f, canvasHeight, 0.9f, blue);
        for (std::size_t index = 0; index < profiles_.size(); ++index) {
            const float y = 302.0f - static_cast<float>(index) * 48.0f;
            if (index == selectedProfile_) drawHighlight(70.0f, canvasHeight - y - 24.0f, 500.0f, 30.0f);
            const bool empty = profiles_[index].empty();
            const SDL_Color color = index == selectedProfile_ ? SDL_Color{0, 0, 0, 255} : gray;
            font_.drawText(renderer_, empty ? "-------" : profiles_[index], 86.0f, y, canvasHeight, 0.9f, color);
            font_.drawText(renderer_, empty ? "----------" : "MISSION 1", 290.0f, y, canvasHeight, 0.9f, color);
            font_.drawText(renderer_, empty ? "--" : "0", 484.0f, y, canvasHeight, 0.9f, color);
        }
        font_.drawText(renderer_, "+ NAV", 60.0f, 16.0f, canvasHeight, 0.75f, gray);
        font_.drawText(renderer_, "X ACCEPT", 200.0f, 16.0f, canvasHeight, 0.75f, gray);
        font_.drawText(renderer_, "[] DELETE", 350.0f, 16.0f, canvasHeight, 0.75f, gray);
        font_.drawText(renderer_, "^ BACK", 505.0f, 16.0f, canvasHeight, 0.75f, gray);
    } else if (screen_ == Screen::nameEntry) {
        drawCentered("CREATE PROFILE", 360.0f);
        drawHighlight(140.0f, 168.0f, 360.0f, 50.0f);
        drawCentered(pendingName_.empty() ? "_" : pendingName_ + "_", 240.0f);
        drawCentered("TYPE A NAME - BACKSPACE TO ERASE", 105.0f, 0.65f);
        drawCentered("X/ENTER  ACCEPT", 62.0f, 0.65f);
    } else {
        drawCentered("MAIN MENU", 415.0f);
        // Recovered ShMenu0 text anchors: (260,280), (250,240), (240,195), (280,150).
        constexpr std::array<SDL_FPoint, 4> anchors{{{260, 280}, {250, 240}, {240, 195}, {280, 150}}};
        for (std::size_t index = 0; index < mainItems.size(); ++index) {
            const float top = canvasHeight - anchors[index].y - 28.0f;
            if (index == selectedMainItem_) drawHighlight(anchors[index].x - 80.0f, top, 330.0f, 34.0f);
            font_.drawText(renderer_, mainItems[index], anchors[index].x, anchors[index].y,
                           canvasHeight, 1.0f);
        }
        drawCentered("UP/DOWN  NAVIGATE     X/ENTER  ACCEPT", 18.0f, 0.65f);
    }
    if (!status_.empty()) drawCentered(status_, 52.0f, 0.58f);
}
}
