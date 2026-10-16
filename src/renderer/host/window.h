#pragma once

#include "../window.h"
#include "../../userconfig.h"
#include <stdexcept>

namespace Impacto::Host {

// Godot owns the platform window and graphics device. SDL is only an event
// queue in an embedded runtime, including on Android and iOS.
class HostWindow final : public BaseWindow {
 public:
  void Init() override {
    SDLWindow = nullptr;
    if (!SDL_InitSubSystem(SDL_INIT_EVENTS))
      throw std::runtime_error(SDL_GetError());
    SetDimensions(UserConfig::CommonSettings.ResolutionWidth,
                  UserConfig::CommonSettings.ResolutionHeight, 0, 1.0f);
  }
  void SetDimensions(int width, int height, int, float) override {
    WindowWidth = width;
    WindowHeight = height;
    MsaaCount = 0;
    RenderScale = 1.0f;
    WindowDimensionsChanged = true;
  }
  void SwapRTs() override {}
  void Update() override {}
  void Draw() override {}
  void Shutdown() override {}
 protected:
  void UpdateDimensions() override {}
};

}  // namespace Impacto::Host
