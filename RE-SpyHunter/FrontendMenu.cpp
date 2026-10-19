#include "FrontendMenu.h"
#include "FrontendCatalogue.h"
#include "../GameUI/MenuDrawing.h"

#include <algorithm>
#include <bit>
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
    "OPERATIONS", "TWO - PLAYER", "SYSTEM OPTIONS", "DOSSIER"
};
// Original float bits from color initialization at SLUS_200.56 VA 0x0019AF08.
constexpr SDL_Color sourceColor(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
{
    const auto byte = [](std::uint32_t bits) { return static_cast<Uint8>(std::bit_cast<float>(bits) * 255.0f + 0.5f); };
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
// The source slider permits three sound modes. STEREO is the captured value 0;
// the other labels remain provisional until the mode enum is recovered.
constexpr std::array<std::string_view, 3> soundModeLabels{"STEREO", "MONO", "SURROUND"};
constexpr std::array<std::string_view, 3> briefingLabels{"OBJECTIVES", "WEAPONS", "ENEMIES"};
constexpr std::array<std::string_view, 2> deleteLabels{"YES", "NO"};

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

bool FrontendMenu::load(std::string& error, const bool debugProfile,
                        const std::optional<Screen> debugStartScreen)
{
    if (!font_.loadFromPackedArchive(root_ / "DATA" / "MENU.PCK", "Eurobet.vkf", renderer_, error))
        return false;
    if (!profileFont_.loadFromPackedArchive(root_ / "DATA" / "MENU.PCK", "euroStile12.vkf", renderer_, error))
        return false;
    if (!menuLightbar_.load(root_ / "DATA" / "SYSTEM.PCK", "menu_lightbar_1pc.rgba", renderer_, error))
        return false;
    if (!profileLightbar_.load(root_ / "DATA" / "MENU.PCK", "menu_lightbar_1pc_long.rgba", renderer_, error))
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
    std::string marker;
    if (input >> marker && marker == "OPTIONS_V1") {
        for (auto& options : options_) {
            Options loaded;
            bool valid = true;
            for (std::size_t index = 0; index < loaded.values.size(); ++index) {
                const int maximum = index == 0 ? 2 : index == 4 ? 1 : 10;
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
        case Screen::settings: parentSelection = 0; break;
        case Screen::controller: parentSelection = 1; break;
        case Screen::extras: parentSelection = 2; break;
        case Screen::musicPlayer: parent = Screen::extras; parentSelection = 0; break;
        case Screen::moviePlayer: parent = Screen::extras; parentSelection = 1; break;
        case Screen::cheatGrid: parent = Screen::extras; parentSelection = 2; break;
        default:
            error = "The debug start screen must be a System Options submenu.";
            return false;
        }
        if (!debugProfile) {
            error = "A debug start screen requires the isolated debug profile.";
            return false;
        }
        profileSelection_.select(0);
        configure(parent, parentSelection);
        open(*debugStartScreen);
    }
    error.clear();
    return true;
}

void FrontendMenu::update()
{
    if (backgroundLoaded_) background_.update();
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
        // Local profiles have no gameplay progress yet: only mission 1 is open.
        if (submenuSelection_.index() != 0) { status_ = "MISSION LOCKED"; return; }
        mission_ = submenuSelection_.index();
        open(Screen::briefing);
    } else if (screen_ == Screen::briefing) {
        status_ = "START MISSION - GAMEPLAY NOT IMPLEMENTED";
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

void FrontendMenu::configure(Screen destination, std::size_t selection)
{
    screen_ = destination;
    horizontalAxis_.reset();
    verticalAxis_.reset();
    menuItems_.clear();
    menuDestinations_.clear();
    menuTitle_.clear();
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
        auto& value = options_[profileSelection_.index()].values[submenuSelection_.index()];
        const int maximum = submenuSelection_.index() == 0 ? 2 : submenuSelection_.index() == 4 ? 1 : 10;
        value = std::clamp(value + direction, 0, maximum);
        status_.clear();
    } else if (screen_ == Screen::briefing) {
        submenuSelection_.move(direction);
    } else if (screen_ == Screen::cheatGrid) {
        submenuSelection_.move(direction);
        status_.clear();
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
        profileFont_.drawText(renderer_, "AGENT", 10.0f, 421.0f, canvasHeight, 0.8f, blue);
        profileFont_.drawText(renderer_, profiles_[profileSelection_.index()], 100.0f, 421.0f, canvasHeight, 0.8f, gray);
        profileFont_.drawText(renderer_, "OBJECTIVES", 10.0f, 408.0f, canvasHeight, 0.8f, blue);
        profileFont_.drawText(renderer_, "0", 162.0f, 408.0f, canvasHeight, 0.8f, gray);
        const float titleScale = 0.9f;
        font_.drawText(renderer_, menuTitle_, canvasWidth - 15.0f - font_.textAdvance(menuTitle_, titleScale),
                       415.0f, canvasHeight, titleScale, titleBlue);
        const bool carousel = isCarousel(screen_);
        if (screen_ == Screen::dossier) {
            ui::drawFilledPanel(renderer_, {35, 88, 570, 285}, {0, 0, 0, 190});
            profileFont_.drawText(renderer_, "MISSION", 50, 350, canvasHeight, 0.8f, titleBlue);
            profileFont_.drawText(renderer_, "TIME", 405, 350, canvasHeight, 0.8f, titleBlue);
            profileFont_.drawText(renderer_, "OBJECTIVES", 480, 350, canvasHeight, 0.7f, titleBlue);
            for (std::size_t index = 0; index < catalogue::missions.size(); ++index) {
                const float y = 327.0f - static_cast<float>(index) * 18.0f;
                profileFont_.drawText(renderer_, catalogue::missions[index], 50, y, canvasHeight, 0.72f, gray);
                profileFont_.drawText(renderer_, "--:--", 405, y, canvasHeight, 0.72f, gray);
                profileFont_.drawText(renderer_, "--", 520, y, canvasHeight, 0.72f, gray);
            }
        }
        if (carousel && screen_ != Screen::cheatGrid) {
            drawCentered(std::to_string(submenuSelection_.index() + 1) + " / " + std::to_string(menuItems_.size()),
                         310.0f, 0.8f);
            std::string_view detail;
            if (screen_ == Screen::missionSelect || screen_ == Screen::twoPlayer)
                detail = screen_ == Screen::missionSelect && submenuSelection_.index() == 0 ? "MISSION OPEN" : "MISSION LOCKED";
            else if (screen_ == Screen::musicPlayer) detail = catalogue::musicAssets[submenuSelection_.index()];
            else if (screen_ == Screen::moviePlayer)
                detail = submenuSelection_.index() < 2 ? "AVAILABLE" : "LOCKED";
            else detail = catalogue::rewardRequirements[submenuSelection_.index()];
            drawCentered(detail, 170, 0.7f);
            drawCentered("LEFT / RIGHT  SELECT", 110, 0.65f);
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
            constexpr std::array<float, 5> settingsBaselines{315, 283, 251, 219, 187};
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
                constexpr std::array<float, 5> selectorWidths{220, 280, 350, 350, 280};
                constexpr std::array<float, 5> selectorBottoms{293, 261, 229, 197, 165};
                sprite = {30.0f, canvasHeight - selectorBottoms[index] - 35.0f,
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
                const SDL_Color settingsOn{112, 151, 255, 255};
                const SDL_Color settingsOff{45, 67, 111, 255};
                profileFont_.drawText(renderer_, menuItems_[index], anchor.x, anchor.y, canvasHeight, 1.0f,
                                      selectedSettingsRow ? settingsOn : settingsOff);
            } else {
                font_.drawText(renderer_, menuItems_[index], anchor.x, anchor.y, canvasHeight, labelScale,
                               selectedSettingsRow ? selected : unselected);
            }
            if (screen_ == Screen::settings) {
                const auto value = options().values[index];
                const SDL_Color valueColor = selectedSettingsRow
                    ? SDL_Color{112, 151, 255, 255} : SDL_Color{47, 65, 103, 255};
                if (index == 0) {
                    profileFont_.drawText(renderer_, soundModeLabels[static_cast<std::size_t>(value)],
                                          270, anchor.y, canvasHeight, 1.0f, valueColor);
                } else if (index == 4) {
                    profileFont_.drawText(renderer_, value ? "ON" : "OFF", 270, anchor.y,
                                          canvasHeight, 1.0f, valueColor);
                } else {
                    constexpr float meterX = 270.0f;
                    constexpr float meterCellWidth = 14.0f;
                    constexpr float meterCellHeight = 18.0f;
                    constexpr float meterGap = 8.0f;
                    constexpr float meterBaselineOffset = 16.0f;
                    const float meterBottom = anchor.y - meterBaselineOffset;
                    const float meterTop = canvasHeight - meterBottom - meterCellHeight;
                    for (int cell = 0; cell < 10; ++cell) {
                        const SDL_FRect cellRect{meterX + cell * (meterCellWidth + meterGap), meterTop,
                                                 meterCellWidth, meterCellHeight};
                        SDL_SetRenderDrawColor(renderer_, 11, 30, 65, 255);
                        SDL_RenderFillRect(renderer_, &cellRect);
                        SDL_SetRenderDrawColor(renderer_, 36, 62, 112, 255);
                        SDL_RenderRect(renderer_, &cellRect);
                        if (cell < value) {
                            const SDL_FRect fillRect{cellRect.x + 3.0f, cellRect.y + 3.0f,
                                cellRect.w - 6.0f, cellRect.h - 6.0f};
                            SDL_SetRenderDrawColor(renderer_, 218, 179, 0, 255);
                            SDL_RenderFillRect(renderer_, &fillRect);
                        }
                    }
                }
            }
        }
        ui::drawFilledPanel(renderer_, {0, 407, canvasWidth, 41}, {0, 0, 0, 255});
        navIcon_.draw(renderer_, 80.0f, 408.0f);
        acceptIcon_.draw(renderer_, 240.0f, 408.0f);
        backIcon_.draw(renderer_, 440.0f, 408.0f);
        font_.drawText(renderer_, "NAV", 120.0f, 15.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "ACCEPT", 280.0f, 15.0f, canvasHeight, 1.0f, gray);
        font_.drawText(renderer_, "BACK", 480.0f, 15.0f, canvasHeight, 1.0f, gray);
    }
    if (!status_.empty()) drawCentered(status_, 52.0f, 0.58f);
}
}
