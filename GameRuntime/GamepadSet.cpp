#include "GamepadSet.h"

namespace spyhunter::runtime
{
GamepadSet::~GamepadSet()
{
    for (const auto& [id, pad] : gamepads_) SDL_CloseGamepad(pad);
}

void GamepadSet::open(SDL_JoystickID id)
{
    if (contains(id) || !SDL_IsGamepad(id)) return;
    if (auto* pad = SDL_OpenGamepad(id)) {
        gamepads_.emplace(id, pad);
        SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(pad));
    } else SDL_Log("Could not open gamepad: %s", SDL_GetError());
}

void GamepadSet::discover()
{
    int count = 0;
    if (auto* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) open(ids[i]);
        SDL_free(ids);
    }
}

void GamepadSet::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) open(event.gdevice.which);
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (const auto found = gamepads_.find(event.gdevice.which); found != gamepads_.end()) {
            SDL_CloseGamepad(found->second);
            gamepads_.erase(found);
            SDL_Log("Gamepad disconnected.");
        }
    }
}
}
