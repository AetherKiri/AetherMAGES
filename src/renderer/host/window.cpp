#include "window.h"
#include "../../impacto.h"
#include "../../inputsystem.h"
#include "../../profile/game.h"

namespace Impacto {
void InitWindow() {
  Window = new Host::HostWindow();
  Window->Init();
}

void SetWindowIcon(SDL_Window*) {}
void InitCursors() {}
void RequestCursor(CursorType type) { ActiveCursorType = type; }
void ApplyCursorForFrame() {}

RectF BaseWindow::GetViewport() {
  const float width = Profile::Game::HasInit ? Profile::Game::DesignWidth : WindowWidth;
  const float height = Profile::Game::HasInit ? Profile::Game::DesignHeight : WindowHeight;
  const float scale = std::min(float(WindowWidth) / width, float(WindowHeight) / height);
  return RectF{(WindowWidth - width * scale) / 2.0f,
               (WindowHeight - height * scale) / 2.0f, width * scale, height * scale};
}
RectF BaseWindow::GetLogicalViewport() { return GetViewport(); }
RectF BaseWindow::GetScaledViewport() {
  auto viewport = GetViewport();
  viewport.X *= RenderScale;
  viewport.Y *= RenderScale;
  viewport.Width *= RenderScale;
  viewport.Height *= RenderScale;
  return viewport;
}
void BaseWindow::ApplyWindowSettings() {
  const auto& settings = UserConfig::ActiveGameSettings();
  SetDimensions(settings.ResolutionWidth.value_or(WindowWidth),
                settings.ResolutionHeight.value_or(WindowHeight), 0, 1.0f);
}
bool HandleWindowEvents(SDL_Event const* event) {
  if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_WINDOW_HIDDEN) {
    Input::ClearTouch();
    return true;
  }
  return false;
}
}  // namespace Impacto
