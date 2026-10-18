#include "FrontendMenu.h"
#include "../GameUI/MenuDrawing.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <span>

namespace spyhunter::game
{
namespace
{
constexpr float canvasWidth = 640.0f;
constexpr float canvasHeight = 448.0f;
constexpr std::array<std::string_view, 4> mainItems{
    "OPERATIONS", "TWO PLAYER", "SYSTEM OPTIONS", "DOSSIER"
};

struct MenuNode {
    FrontendMenu::Screen screen;
    std::string_view title;
    std::span<const std::string_view> labels;
    std::span<const FrontendMenu::Screen> destinations;
};

constexpr std::array<std::string_view, 2> operationsLabels{"MISSION SELECT", "RENDEZVOUS"};
constexpr std::array<FrontendMenu::Screen, 2> operationsDestinations{
    FrontendMenu::Screen::missionSelect, FrontendMenu::Screen::rendezvous};
constexpr std::array<std::string_view, 2> twoPlayerLabels{"PLAYER 1", "PLAYER 2"};
constexpr std::array<FrontendMenu::Screen, 0> noDestinations{};
constexpr std::array<FrontendMenu::Screen, 0> twoPlayerDestinations{};
constexpr std::array<std::string_view, 2> systemOptionsLabels{"SETTINGS", "CONTROLLER"};
constexpr std::array<FrontendMenu::Screen, 2> systemOptionsDestinations{
    FrontendMenu::Screen::settings, FrontendMenu::Screen::controller};
constexpr std::array<std::string_view, 2> dossierLabels{"EXTRAS", "MISSION DATA"};
constexpr std::array<FrontendMenu::Screen, 2> dossierDestinations{
    FrontendMenu::Screen::extras, FrontendMenu::Screen::missionData};
constexpr std::array<std::string_view, 3> extrasLabels{"MUSIC PLAYER", "MOVIE PLAYER", "CHEAT GRID"};
constexpr std::array<FrontendMenu::Screen, 3> extrasDestinations{
    FrontendMenu::Screen::musicPlayer, FrontendMenu::Screen::moviePlayer, FrontendMenu::Screen::cheatGrid};
constexpr std::array<std::string_view, 2> controllerLabels{"CONTROLLER 1 OPTIONS", "CONTROLLER 2 OPTIONS"};
constexpr std::array<FrontendMenu::Screen, 2> controllerDestinations{
    FrontendMenu::Screen::controller1Options, FrontendMenu::Screen::controller2Options};
constexpr std::array<std::string_view, 3> controllerOptionsLabels{"STANDARD", "REVERSED", "FLIPPED"};
constexpr std::array<std::string_view, 1> missionSelectLabels{"MISSION SELECT"};
constexpr std::array<std::string_view, 1> rendezvousLabels{"RENDEZVOUS"};
constexpr std::array<std::string_view, 1> settingsLabels{"SETTINGS"};
constexpr std::array<std::string_view, 1> missionDataLabels{"MISSION DATA"};
constexpr std::array<std::string_view, 1> musicPlayerLabels{"MUSIC PLAYER"};
constexpr std::array<std::string_view, 1> moviePlayerLabels{"MOVIE PLAYER"};
constexpr std::array<std::string_view, 1> cheatGridLabels{"CHEAT GRID"};

std::optional<MenuNode> menuNode(const FrontendMenu::Screen screen)
{
    using Screen = FrontendMenu::Screen;
    switch (screen) {
    case Screen::operations: return MenuNode{screen, "OPERATIONS", operationsLabels, operationsDestinations};
    case Screen::twoPlayer: return MenuNode{screen, "TWO PLAYER", twoPlayerLabels, twoPlayerDestinations};
    case Screen::systemOptions: return MenuNode{screen, "SYSTEM OPTIONS", systemOptionsLabels, systemOptionsDestinations};
    case Screen::dossier: return MenuNode{screen, "DOSSIER", dossierLabels, dossierDestinations};
    case Screen::extras: return MenuNode{screen, "EXTRAS", extrasLabels, extrasDestinations};
    case Screen::controller: return MenuNode{screen, "CONTROLLER", controllerLabels, controllerDestinations};
    case Screen::controller1Options: case Screen::controller2Options:
        return MenuNode{screen, "CONTROLLER OPTIONS", controllerOptionsLabels, noDestinations};
    case Screen::missionSelect: return MenuNode{screen, "MISSION SELECT", missionSelectLabels, noDestinations};
    case Screen::rendezvous: return MenuNode{screen, "RENDEZVOUS", rendezvousLabels, noDestinations};
    case Screen::settings: return MenuNode{screen, "SETTINGS", settingsLabels, noDestinations};
    case Screen::missionData: return MenuNode{screen, "MISSION DATA", missionDataLabels, noDestinations};
    case Screen::musicPlayer: return MenuNode{screen, "MUSIC PLAYER", musicPlayerLabels, noDestinations};
    case Screen::moviePlayer: return MenuNode{screen, "MOVIE PLAYER", moviePlayerLabels, noDestinations};
    case Screen::cheatGrid: return MenuNode{screen, "CHEAT GRID", cheatGridLabels, noDestinations};
    default: return std::nullopt;
    }
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

bool FrontendMenu::load(std::string& error)
{
    if (!font_.loadFromPackedArchive(root_ / "DATA" / "MENU.PCK", "Eurobet.vkf", renderer_, error))
        return false;
    if (!profileFont_.loadFromPackedArchive(root_ / "DATA" / "MENU.PCK", "euroStile12.vkf", renderer_, error))
        return false;
    const auto systemArchive = root_ / "DATA" / "SYSTEM.PCK";
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
        profileSelection_.move(direction);
    } else if (screen_ == Screen::mainMenu || menuNode(screen_)) {
        if (screen_ != Screen::mainMenu) {
            submenuSelection_.move(direction);
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
    Screen destination;
    switch (mainSelection_.index()) {
    case 0: destination = Screen::operations; break;
    case 1: destination = Screen::twoPlayer; break;
    case 2: destination = Screen::systemOptions; break;
    case 3: destination = Screen::dossier; break;
    default: return;
    }
    if (screen_ != Screen::mainMenu) {
        if (submenuSelection_.index() >= menuDestinations_.size()) {
            status_ = std::string(menuItems_[submenuSelection_.index()]) + " IS NOT YET AVAILABLE";
            return;
        }
        destination = menuDestinations_[submenuSelection_.index()];
        menuSelectionStack_.push_back(submenuSelection_.index());
        menuStack_.push_back(screen_);
    } else {
        menuStack_.push_back(Screen::mainMenu);
    }
    screen_ = destination;
    const auto node = menuNode(screen_);
    menuTitle_ = node->title;
    menuItems_.assign(node->labels.begin(), node->labels.end());
    menuDestinations_.assign(node->destinations.begin(), node->destinations.end());
    submenuSelection_ = ui::MenuSelection(menuItems_.size());
    status_.clear();
}

void FrontendMenu::back()
{
    status_.clear();
    if (menuNode(screen_)) {
        if (!menuStack_.empty()) {
            screen_ = menuStack_.back();
            menuStack_.pop_back();
            if (screen_ == Screen::mainMenu) {
                menuTitle_.clear();
                menuItems_.clear();
                menuDestinations_.clear();
            } else if (const auto node = menuNode(screen_)) {
                menuTitle_ = node->title;
                menuItems_.assign(node->labels.begin(), node->labels.end());
                menuDestinations_.assign(node->destinations.begin(), node->destinations.end());
                const auto previousSelection = menuSelectionStack_.back();
                menuSelectionStack_.pop_back();
                submenuSelection_ = ui::MenuSelection(menuItems_.size());
                for (std::size_t index = 0; index < previousSelection; ++index) submenuSelection_.move(1);
            }
        }
    } else if (screen_ == Screen::mainMenu || screen_ == Screen::nameEntry) {
        screen_ = Screen::profileSelect;
    }
}

void FrontendMenu::handleEvent(const SDL_Event& event)
{
    if (isBack(event)) { back(); return; }
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

void FrontendMenu::render() const
{
    SDL_SetRenderDrawColor(renderer_, 3, 13, 35, 255);
    SDL_RenderClear(renderer_);
    const SDL_FRect viewport{0, 0, canvasWidth, canvasHeight};
    if (backgroundLoaded_) background_.render(renderer_, viewport);

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
            const float y = 302.0f - static_cast<float>(index) * 35.0f;
            if (index == profileSelection_.index())
                ui::drawPillSelector(renderer_, {70.0f, canvasHeight - y - 24.0f, 500.0f, 22.0f}, {31, 91, 139, 230});
            const bool empty = profiles_[index].empty();
            const SDL_Color color = index == profileSelection_.index() ? SDL_Color{0, 0, 0, 255} : gray;
            profileFont_.drawText(renderer_, empty ? "--------" : profiles_[index], 86.0f, y, canvasHeight, 1.0f, color);
            profileFont_.drawText(renderer_, empty ? "----------" : "MISSION 1", 290.0f, y, canvasHeight, 1.0f, color);
            profileFont_.drawText(renderer_, empty ? "  --" : "0", 484.0f, y, canvasHeight, 1.0f, color);
        }
        navIcon_.draw(renderer_, 30.0f, 408.0f);
        acceptIcon_.draw(renderer_, 160.0f, 408.0f);
        backIcon_.draw(renderer_, 490.0f, 408.0f);
        font_.drawText(renderer_, "NAV", 70.0f, 24.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "ACCEPT", 200.0f, 24.0f, canvasHeight, 1.0f, gray);
        if (!profiles_[profileSelection_.index()].empty()) {
            deleteIcon_.draw(renderer_, 336.0f, 408.0f);
            font_.drawText(renderer_, "DELETE", 380.0f, 24.0f, canvasHeight, 1.0f, gray);
        }
        font_.drawText(renderer_, "BACK", 530.0f, 24.0f, canvasHeight, 1.0f, gray);
    } else if (screen_ == Screen::nameEntry) {
        drawCentered("CREATE PROFILE", 360.0f);
        ui::drawPillSelector(renderer_, {140.0f, 168.0f, 360.0f, 50.0f}, {31, 54, 128, 220});
        drawCentered(pendingName_.empty() ? "_" : pendingName_ + "_", 240.0f);
        drawCentered("TYPE A NAME - BACKSPACE TO ERASE", 105.0f, 0.65f);
        drawCentered("X/ENTER  ACCEPT", 62.0f, 0.65f);
    } else if (screen_ == Screen::mainMenu) {
        const SDL_Color blue{30, 145, 225, 255};
        const SDL_Color selected{126, 158, 242, 255};
        const SDL_Color unselected{2, 20, 44, 255};
        const SDL_Color gray{180, 190, 200, 255};
        font_.drawText(renderer_, "AGENT", 10.0f, 415.0f, canvasHeight, 0.72f, blue);
        font_.drawText(renderer_, profiles_[profileSelection_.index()], 100.0f, 415.0f, canvasHeight, 0.72f, gray);
        font_.drawText(renderer_, "OBJECTIVES", 10.0f, 397.0f, canvasHeight, 0.72f, blue);
        font_.drawText(renderer_, "0", 160.0f, 397.0f, canvasHeight, 0.72f, gray);
        font_.drawText(renderer_, "MAIN MENU", 473.0f, 415.0f, canvasHeight, 1.0f, blue);
        // Recovered ShMenu0 text anchors: (260,280), (250,240), (240,195), (280,150).
        constexpr std::array<SDL_FPoint, 4> anchors{{{260, 280}, {250, 240}, {240, 195}, {280, 150}}};
        for (std::size_t index = 0; index < mainItems.size(); ++index) {
            const float top = canvasHeight - anchors[index].y - 28.0f;
            if (index == mainSelection_.index())
                ui::drawPillSelector(renderer_, {236.0f, top + 12.0f, 174.0f, 22.0f}, {31, 54, 128, 220});
            font_.drawText(renderer_, mainItems[index], anchors[index].x, anchors[index].y,
                           canvasHeight, 1.0f, index == mainSelection_.index() ? selected : unselected);
        }
        ui::drawFilledPanel(renderer_, {0, 390, canvasWidth, 52}, {4, 30, 58, 220});
        navIcon_.draw(renderer_, 80.0f, 408.0f);
        acceptIcon_.draw(renderer_, 240.0f, 408.0f);
        backIcon_.draw(renderer_, 440.0f, 408.0f);
        font_.drawText(renderer_, "NAV", 120.0f, 24.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "ACCEPT", 280.0f, 24.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "BACK", 480.0f, 24.0f, canvasHeight, 1.0f, gray);
    } else if (menuNode(screen_)) {
        const SDL_Color blue{30, 145, 225, 255};
        const SDL_Color selected{126, 158, 242, 255};
        const SDL_Color unselected{2, 20, 44, 255};
        const SDL_Color gray{180, 190, 200, 255};
        font_.drawText(renderer_, "AGENT", 10.0f, 415.0f, canvasHeight, 0.72f, blue);
        font_.drawText(renderer_, profiles_[profileSelection_.index()], 100.0f, 415.0f, canvasHeight, 0.72f, gray);
        font_.drawText(renderer_, "OBJECTIVES", 10.0f, 397.0f, canvasHeight, 0.72f, blue);
        font_.drawText(renderer_, "0", 160.0f, 397.0f, canvasHeight, 0.72f, gray);
        font_.drawText(renderer_, menuTitle_, 465.0f, 415.0f, canvasHeight, 1.0f, blue);
        constexpr float itemStep = 40.0f;
        for (std::size_t index = 0; index < menuItems_.size(); ++index) {
            const SDL_FPoint anchor{240.0f, 280.0f - static_cast<float>(index) * itemStep};
            if (index == submenuSelection_.index()) {
                const float top = canvasHeight - anchor.y - 28.0f;
                ui::drawPillSelector(renderer_, {210.0f, top + 12.0f, 220.0f, 22.0f}, {31, 54, 128, 220});
            }
            font_.drawText(renderer_, menuItems_[index], anchor.x, anchor.y, canvasHeight, 1.0f,
                           index == submenuSelection_.index() ? selected : unselected);
        }
        ui::drawFilledPanel(renderer_, {0, 390, canvasWidth, 52}, {4, 30, 58, 220});
        navIcon_.draw(renderer_, 80.0f, 408.0f);
        acceptIcon_.draw(renderer_, 240.0f, 408.0f);
        backIcon_.draw(renderer_, 440.0f, 408.0f);
        font_.drawText(renderer_, "NAV", 120.0f, 24.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "ACCEPT", 280.0f, 24.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "BACK", 480.0f, 24.0f, canvasHeight, 1.0f, gray);
    }
    if (!status_.empty()) drawCentered(status_, 52.0f, 0.58f);
}
}
