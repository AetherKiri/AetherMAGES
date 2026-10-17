#pragma once

#include "config.h"
#include <SDL3/SDL_events.h>

namespace Impacto::Events {
// Embedded input and worker completions never enter SDL's platform event pump.
// TEXT_INPUT payloads are copied and remain valid until the next Poll call.
bool Push(const SDL_Event* event);
bool Poll(SDL_Event* event);
bool PollType(SDL_Event* event, Uint32 type);
Uint32 Register();
void Reset();
}  // namespace Impacto::Events
