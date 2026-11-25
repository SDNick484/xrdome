#include "app/controls_keys.h"

#include <SDL_keycode.h>

bool xrd_key_repeats(int32_t sdl_keycode)
{
    switch (sdl_keycode) {
    case SDLK_LEFT:
    case SDLK_RIGHT:
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFTBRACKET:
    case SDLK_RIGHTBRACKET:
    case SDLK_MINUS:
    case SDLK_EQUALS:
    case SDLK_COMMA:
    case SDLK_PERIOD:
        return true;
    default:
        return false;
    }
}
