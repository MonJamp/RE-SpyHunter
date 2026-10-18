#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>

namespace spyhunter::runtime
{
// Owns open controllers, not game-specific button mappings. Destroy before SDL_Quit.
class GamepadSet
{
public:
    GamepadSet() = default;
    ~GamepadSet();
    GamepadSet(const GamepadSet&) = delete;
    GamepadSet& operator=(const GamepadSet&) = delete;
    void discover();
    void handleEvent(const SDL_Event& event);
    bool contains(SDL_JoystickID id) const { return gamepads_.contains(id); }

private:
    void open(SDL_JoystickID id);
    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> gamepads_;
};
}
