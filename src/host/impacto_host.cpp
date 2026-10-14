#include "impacto_host.h"

#include "hostbridge.h"

#include "../game.h"
#include "../log.h"
#include "../profile/profile.h"
#include "../profile/gamedefinitions.h"
#include "../userconfig.h"
#include "../audio/host/hostaudiobridge.h"
#include "../renderer/renderer.h"
#include "../renderer/window.h"

#ifndef IMPACTO_DISABLE_OPENGL
#include <glad/glad.h>
#include "../renderer/opengl/renderer.h"
#include "../renderer/opengl/window.h"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

/* The impacto_host_scancode_t values are contract constants; keep them
 * verified against the real SDL enum at build time. */
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_A) == static_cast<int>(SDL_SCANCODE_A));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_Z) == static_cast<int>(SDL_SCANCODE_Z));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_1) == static_cast<int>(SDL_SCANCODE_1));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_0) == static_cast<int>(SDL_SCANCODE_0));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_RETURN) == static_cast<int>(SDL_SCANCODE_RETURN));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_ESCAPE) == static_cast<int>(SDL_SCANCODE_ESCAPE));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_BACKSPACE) == static_cast<int>(SDL_SCANCODE_BACKSPACE));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_TAB) == static_cast<int>(SDL_SCANCODE_TAB));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_SPACE) == static_cast<int>(SDL_SCANCODE_SPACE));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_F1) == static_cast<int>(SDL_SCANCODE_F1));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_F12) == static_cast<int>(SDL_SCANCODE_F12));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_PAGEUP) == static_cast<int>(SDL_SCANCODE_PAGEUP));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_PAGEDOWN) == static_cast<int>(SDL_SCANCODE_PAGEDOWN));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_HOME) == static_cast<int>(SDL_SCANCODE_HOME));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_END) == static_cast<int>(SDL_SCANCODE_END));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_DELETE) == static_cast<int>(SDL_SCANCODE_DELETE));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_INSERT) == static_cast<int>(SDL_SCANCODE_INSERT));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_LEFT) == static_cast<int>(SDL_SCANCODE_LEFT));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_UP) == static_cast<int>(SDL_SCANCODE_UP));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_RIGHT) == static_cast<int>(SDL_SCANCODE_RIGHT));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_DOWN) == static_cast<int>(SDL_SCANCODE_DOWN));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_LCTRL) == static_cast<int>(SDL_SCANCODE_LCTRL));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_LSHIFT) == static_cast<int>(SDL_SCANCODE_LSHIFT));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_LALT) == static_cast<int>(SDL_SCANCODE_LALT));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_RCTRL) == static_cast<int>(SDL_SCANCODE_RCTRL));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_RSHIFT) == static_cast<int>(SDL_SCANCODE_RSHIFT));
static_assert(static_cast<int>(IMPACTO_HOST_SCANCODE_RALT) == static_cast<int>(SDL_SCANCODE_RALT));

namespace {

using ImpactoHostResult = int32_t;

/*
 * Window stand-in used during teardown: Game::Shutdown() ends with
 * Window->Shutdown() and the real GLWindow::Shutdown() calls exit(0), which
 * would kill the host process. Swapping the global Window pointer to one of
 * these for the duration of Game::Shutdown() keeps the engine alive; the GL
 * resources are then released explicitly by the host library.
 */
class NullWindow : public Impacto::BaseWindow {
 public:
  void Init() override {}
  void SetDimensions(int width, int height, int msaa,
                     float renderScale) override {}
  void SwapRTs() override {}
  void Update() override {}
  void Draw() override {}
  void Shutdown() override {}

 protected:
  void UpdateDimensions() override {}
};

struct HostState {
  // Description, fixed at create time.
  std::string DataDir;
  std::string WritableDir;
  uint32_t InitialWidth = 1280;
  uint32_t InitialHeight = 720;
  Impacto::LogLevel Level = Impacto::LogLevel::Warning;

  // Engine thread machinery. All engine entry points run on the thread; the
  // host-facing calls block until the posted command finished.
  std::thread Thread;
  std::mutex QueueMutex;
  std::condition_variable QueueCV;
  struct Command {
    std::function<ImpactoHostResult()> Fn;
    std::promise<ImpactoHostResult> Done;
  };
  std::deque<Command> Queue;
  bool QuitRequested = false;  // guarded by QueueMutex

  std::atomic<bool> GameOpened{false};
  // Opt-in soak acceleration. Reads always render the current state.
  uint32_t RenderInterval = 1;
  uint64_t TickCount = 0;
  bool RenderPending = false;
  std::atomic<bool> EngineAlive{false};
  std::atomic<bool> Paused{false};
  std::atomic<bool> ShouldQuit{false};
  std::atomic<uint32_t> PendingWidth{0};
  std::atomic<uint32_t> PendingHeight{0};
};

std::mutex g_InstanceMutex;
HostState* g_Instance = nullptr;

ImpactoHostResult PostCommand(HostState* st, std::function<ImpactoHostResult()> fn) {
  if (!st->EngineAlive.load()) return IMPACTO_HOST_INVALID_STATE;
  std::future<ImpactoHostResult> future;
  {
    std::lock_guard<std::mutex> lock(st->QueueMutex);
    if (st->QuitRequested) return IMPACTO_HOST_INVALID_STATE;
    HostState::Command cmd;
    cmd.Fn = std::move(fn);
    future = cmd.Done.get_future();
    st->Queue.push_back(std::move(cmd));
  }
  st->QueueCV.notify_one();
  return future.get();
}

void EngineLoop(HostState* st) {
  st->EngineAlive.store(true);
  while (true) {
    HostState::Command cmd;
    {
      std::unique_lock<std::mutex> lock(st->QueueMutex);
      st->QueueCV.wait(lock,
                       [&] { return st->QuitRequested || !st->Queue.empty(); });
      if (st->Queue.empty() && st->QuitRequested) break;
      cmd = std::move(st->Queue.front());
      st->Queue.pop_front();
    }

    ImpactoHostResult result = IMPACTO_HOST_OK;
    try {
      result = cmd.Fn();
    } catch (std::exception const& e) {
      Impacto::Host::HostBridge::Get().SetEngineThreadError(e.what());
      Impacto::ImpLog(Impacto::LogLevel::Error, Impacto::LogChannel::General,
                      "Host bridge: engine exception: {:s}\n", e.what());
      result = IMPACTO_HOST_INTERNAL_ERROR;
    } catch (...) {
      Impacto::Host::HostBridge::Get().SetEngineThreadError(
          "unknown engine exception");
      result = IMPACTO_HOST_INTERNAL_ERROR;
    }
    cmd.Done.set_value(result);
  }
  st->EngineAlive.store(false);
}

std::string EscapeLuaString(std::string const& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

#ifndef IMPACTO_DISABLE_OPENGL

// Read the render target the engine just drew into and publish it as a
// top-down RGBA8888 frame. Must run on the engine thread (GL context owner).
// This only *reads* backend state; no backend code is modified.
void CaptureFrame() {
  using namespace Impacto;
  if (Window == nullptr || Renderer == nullptr) return;

  auto* glWindow = static_cast<OpenGL::GLWindow*>(Window);
  if (glWindow->DrawRT == 0 || glWindow->GLContext == nullptr) return;

  RectF viewport = glWindow->GetViewport();
  const int width = static_cast<int>(viewport.Width);
  const int height = static_cast<int>(viewport.Height);
  if (width <= 0 || height <= 0) return;

  const size_t stride = static_cast<size_t>(width) * 4;
  std::vector<uint8_t> pixels(stride * height);

  GLint previousReadFramebuffer = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, glWindow->DrawRT);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);

  // GL framebuffers are bottom-up; the host expects top-down rows.
  std::vector<uint8_t> flipped(pixels.size());
  for (int row = 0; row < height; ++row) {
    std::memcpy(flipped.data() + static_cast<size_t>(row) * stride,
                pixels.data() + static_cast<size_t>(height - 1 - row) * stride,
                stride);
  }

  Host::HostBridge::Get().PublishFrame(std::move(flipped),
                                        static_cast<uint32_t>(width),
                                        static_cast<uint32_t>(height));
}

ImpactoHostResult DoOpenGame(HostState* st, std::string gameRoot,
                             std::string gameId, std::string gamedataDir);
void TeardownEngine(HostState* st);

// Every shipped MAGES. profile mounts its archives from
// <gamedata>/<game-id>/ with a script.* or system.* archive at the root of
// that subdirectory. Candidates are taken from the gamedefinitions.lua
// registry so an unrecognised directory never wins; ties resolve by the
// definition's launcher order (then id) to stay deterministic.
static bool HasScriptOrSystemArchive(const std::filesystem::path& gameDir) {
  std::error_code ec;
  std::filesystem::directory_iterator it(gameDir, ec);
  if (ec) return false;
  for (auto const& entry : it) {
    std::error_code entryEc;
    if (!entry.is_regular_file(entryEc) || entryEc) continue;
    std::string name = entry.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (name.rfind("script.", 0) == 0 || name.rfind("system.", 0) == 0) {
      return true;
    }
  }
  return false;
}

static std::optional<std::string> DetectGameId(
    const std::filesystem::path& gamedataDir) {
  struct Candidate {
    std::string id;
    int launcherOrderId;
  };
  std::vector<Candidate> candidates;
  std::error_code ec;
  for (auto const& [id, definition] : Impacto::Profile::GameDefinitions) {
    const std::filesystem::path gameDir = gamedataDir / id;
    if (!std::filesystem::is_directory(gameDir, ec)) continue;
    if (!HasScriptOrSystemArchive(gameDir)) continue;
    candidates.push_back({id, definition.LauncherOrderId});
  }
  if (candidates.empty()) return std::nullopt;
  std::sort(candidates.begin(), candidates.end(),
            [](Candidate const& a, Candidate const& b) {
              if (a.launcherOrderId != b.launcherOrderId) {
                return a.launcherOrderId < b.launcherOrderId;
              }
              return a.id < b.id;
            });
  if (candidates.size() > 1) {
    std::string others;
    for (size_t i = 1; i < candidates.size(); ++i) {
      if (i > 1) others += ", ";
      others += candidates[i].id;
    }
    ImpLog(Impacto::LogLevel::Warning, Impacto::LogChannel::General,
           "Host bridge: multiple MAGES. games found in game root; using "
           "\"{:s}\" (also present: {:s})\n",
           candidates.front().id, others);
  }
  return candidates.front().id;
}

ImpactoHostResult DoOpenGame(HostState* st, std::string gameRoot,
                             std::string gameId, std::string gamedataDir) {
  using namespace Impacto;
  if (st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;

  Host::HostBridge::Get().Reset();

  // Game selection: "launcher" boots the engine without an active game (an
  // empty frame per tick, the no-game-data smoke path); "auto" (or an empty
  // id) resolves the game from the gamedefinitions.lua registry against the
  // standard MAGES. install layout; any other id must name a registered
  // game.
  const bool launcherMode = (gameId == "launcher");
  const bool autoDetect = gameId.empty() || gameId == "auto";

  std::error_code ec;
  std::filesystem::path dataDir =
      std::filesystem::absolute(st->DataDir, ec);
  if (ec) return IMPACTO_HOST_IO_ERROR;
  std::filesystem::path gameRootPath =
      std::filesystem::absolute(gameRoot, ec);
  if (ec) return IMPACTO_HOST_IO_ERROR;
  std::filesystem::path gamedataPath = gamedataDir.empty()
                                           ? gameRootPath
                                           : std::filesystem::absolute(gamedataDir, ec);
  if (ec) return IMPACTO_HOST_IO_ERROR;

  if (!std::filesystem::exists(dataDir / "gamedefinitions.lua")) {
    Host::HostBridge::Get().SetEngineThreadError(
        "data_dir does not contain gamedefinitions.lua");
    return IMPACTO_HOST_IO_ERROR;
  }
  if (!std::filesystem::is_directory(gameRootPath)) {
    Host::HostBridge::Get().SetEngineThreadError("game root is not a directory");
    return IMPACTO_HOST_IO_ERROR;
  }

  std::filesystem::path savesDir = std::filesystem::path(st->WritableDir) / "saves";
  std::filesystem::create_directories(savesDir, ec);
  if (ec) return IMPACTO_HOST_IO_ERROR;
  std::filesystem::path basePathsPath =
      std::filesystem::path(st->WritableDir) / "basepaths.lua";

  {
    std::ofstream out(basePathsPath, std::ios::binary | std::ios::trunc);
    if (!out) return IMPACTO_HOST_IO_ERROR;
    out << "root.BasePaths = {\n"
        << "  RootInstallDir = \"" << EscapeLuaString(gameRootPath.string())
        << "\",\n"
        << "  RootGamedataDir = \""
        << EscapeLuaString(gamedataPath.string()) << "\",\n"
        << "  RootProfilesDir = \"" << EscapeLuaString((dataDir / "profiles").string())
        << "\",\n"
        << "  RootPatchesDir = \"\",\n"
        << "  RootSavesDir = \"" << EscapeLuaString(savesDir.string())
        << "\",\n"
        << "};\n";
  }

  const uint32_t width = st->PendingWidth.load() ? st->PendingWidth.load()
                                                 : st->InitialWidth;
  const uint32_t height = st->PendingHeight.load() ? st->PendingHeight.load()
                                                   : st->InitialHeight;

  // The profile system and shader loader resolve several paths against the
  // current working directory, so run the boot sequence from data_dir.
  std::filesystem::path oldCwd = std::filesystem::current_path(ec);
  if (ec) return IMPACTO_HOST_IO_ERROR;
  std::filesystem::current_path(dataDir, ec);
  if (ec) return IMPACTO_HOST_IO_ERROR;

  // Headless by construction: pick SDL's offscreen video driver for engine
  // initialisation, restore the previous setting afterwards so other SDL users
  // in this process are unaffected. The hint (not the environment) is the
  // modern SDL3 way and is scoped to this process' SDL state.
  const char* oldDriverHint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                          SDL_HINT_OVERRIDE);

  ImpactoHostResult result = IMPACTO_HOST_OK;
  try {
    Profile::BasePathsPath = basePathsPath.string();
    Profile::GameDefinitionsPath = (dataDir / "gamedefinitions.lua").string();
    UserConfig::UserConfigPath =
        (std::filesystem::path(st->WritableDir) / "userconfig.toml").string();

    UserConfig::CommonSettings.LogLvl = st->Level;
    UserConfig::CommonSettings.LogChannels = LogChannel::All;
    UserConfig::CommonSettings.LoggingToConsole = true;
    UserConfig::CommonSettings.LoggingToFile = false;

    Profile::Init();
    Profile::Configure();

    // Resolve the game id before anything engine-side is created: the
    // registry is pure Lua state at this point, so an unknown id or an
    // undetected root fails without a teardown-worthy half-boot.
    if (!launcherMode) {
      if (autoDetect) {
        auto detected = DetectGameId(gamedataPath);
        if (!detected) {
          throw std::runtime_error(
              "no MAGES. game data found in \"" + gamedataPath.string() +
              "\" (expected a <game-id>/script.* subdirectory of a game "
              "registered in gamedefinitions.lua)");
        }
        gameId = *detected;
        ImpLog(LogLevel::Info, LogChannel::General,
               "Host bridge: auto-detected game \"{:s}\"\n", gameId);
      } else if (!Profile::GameDefinitions.contains(gameId)) {
        throw std::runtime_error("game id \"" + gameId +
                                 "\" is not registered in "
                                 "gamedefinitions.lua");
      }
      UserConfig::SetActiveGame(gameId);
    }

    // Force the embedded configuration: software-friendly OpenGL renderer,
    // audio routed into the host FIFO bridge instead of a device backend.
    UserConfig::AdvancedSettings.ActiveRenderer = RendererType::OpenGL;
    UserConfig::AdvancedSettings.ActiveAudioBackend =
        AudioBackendType::Host;
    UserConfig::CommonSettings.LogLvl = st->Level;
    UserConfig::CommonSettings.LoggingToConsole = true;
    UserConfig::CommonSettings.LoggingToFile = false;
    UserConfig::PatchProfileOverride.clear();

    auto& gameSettings = UserConfig::ActiveGameSettings();
    gameSettings.UsePatch = false;
    gameSettings.PatchProfile.clear();
    gameSettings.ResolutionWidth = (int)width;
    gameSettings.ResolutionHeight = (int)height;
    gameSettings.Display = DisplayMode::Windowed;
    UserConfig::CommonSettings.ResolutionWidth = (int)width;
    UserConfig::CommonSettings.ResolutionHeight = (int)height;

    Game::Init();
    if (!launcherMode) {
      Game::InitGameProfile();
    }

    // The window may have been clamped to the (small) offscreen display
    // bounds during creation; force the requested presentation size so the
    // readback surface matches the design resolution.
    if (Window != nullptr) {
      Window->MsaaCount = 0;
      Window->RenderScale = 1.0f;
      Window->SetDimensions((int)width, (int)height, 0, 1.0f);
    }
  } catch (std::exception const& e) {
    Host::HostBridge::Get().SetEngineThreadError(e.what());
    ImpLog(LogLevel::Error, LogChannel::General,
           "Host bridge: open_game failed: {:s}\n", e.what());
    result = IMPACTO_HOST_INTERNAL_ERROR;
  } catch (...) {
    Host::HostBridge::Get().SetEngineThreadError("unknown error during open");
    result = IMPACTO_HOST_INTERNAL_ERROR;
  }

  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, oldDriverHint,
                          SDL_HINT_OVERRIDE);
  std::error_code restoreEc;
  std::filesystem::current_path(oldCwd, restoreEc);

  if (result != IMPACTO_HOST_OK) {
    // The engine half-initialised: it already owns the window, GL context
    // and worker threads. Tear them down so a failed open does not leave
    // joinable globals that would terminate() at process exit.
    TeardownEngine(st);
    return result;
  }

  Host::HostBridge::Get().SetRendererInfo(fmt::format(
      "OpenGL (offscreen, llvmpipe-safe), {:d}x{:d}", width, height));

  // Produce a first frame so the host can present immediately.
  if (UserConfig::GetActiveGame().empty()) {
    Game::LauncherUpdate(1.0f / 60.0f);
    Game::LauncherRender();
  } else {
    Game::Update(1.0f / 60.0f);
    Game::Render();
  }
  CaptureFrame();

  st->GameOpened.store(true);
  st->ShouldQuit.store(false);
  ImpLog(LogLevel::Info, LogChannel::General,
         "Host bridge: game \"{:s}\" opened ({:d}x{:d})\n", gameId, width,
         height);
  return IMPACTO_HOST_OK;
}

void RenderAndCapture(HostState* st) {
  using namespace Impacto;
  if (UserConfig::GetActiveGame().empty()) {
    Game::LauncherRender();
  } else {
    Game::Render();
  }
  CaptureFrame();
  st->RenderPending = false;
}

ImpactoHostResult DoTick(HostState* st, uint32_t deltaMs) {
  using namespace Impacto;
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;
  if (st->Paused.load()) return IMPACTO_HOST_OK;
  if (st->ShouldQuit.load() || Game::ShouldQuit) {
    st->ShouldQuit.store(true);
    return IMPACTO_HOST_OK;
  }

  float dt = (float)deltaMs / 1000.0f;
  if (dt <= 0.0f) dt = 1.0f / 60.0f;
  if (dt > 0.25f) dt = 0.25f;

  if (UserConfig::GetActiveGame().empty()) {
    Game::LauncherUpdate(dt);
  } else {
    Game::Update(dt);
  }
  st->RenderPending = true;
  if (++st->TickCount % st->RenderInterval == 0) RenderAndCapture(st);

  if (Game::ShouldQuit) st->ShouldQuit.store(true);
  return IMPACTO_HOST_OK;
}

// Full engine teardown, shared by the successful-destroy path and the
// open_game failure path (a half-initialised engine still owns the window,
// GL context and worker threads; leaving them alive would terminate() at
// process exit when the global thread pool is destroyed joinable).
// Must run on the engine thread.
void TeardownEngine(HostState* st) {
  using namespace Impacto;
  if (Window == nullptr && Renderer == nullptr) return;

  // Game::Shutdown() ends in Window->Shutdown(); the GL implementation of
  // that calls exit(0). Swap in a no-op window for the duration so the host
  // process survives, then tear the context down ourselves.
  BaseWindow* realWindow = Window;
  NullWindow nullWindow;
  if (realWindow != nullptr) Window = &nullWindow;
  try {
    Game::Shutdown();
  } catch (std::exception const& e) {
    ImpLog(LogLevel::Error, LogChannel::General,
           "Host bridge: exception during engine shutdown: {:s}\n", e.what());
  } catch (...) {
  }
  if (realWindow != nullptr) Window = realWindow;

  if (Window != nullptr) {
    auto* glWindow = static_cast<OpenGL::GLWindow*>(Window);
    if (glWindow->GLContext != nullptr) {
      SDL_GL_MakeCurrent(glWindow->SDLWindow, glWindow->GLContext);
    }
    // The renderer destructor still issues GL calls; delete it while the
    // context is alive, then destroy the context and window.
    if (Renderer != nullptr) {
      delete static_cast<OpenGL::Renderer*>(Renderer);
      Renderer = nullptr;
    }
    SDL_GL_DestroyContext(glWindow->GLContext);
    SDL_DestroyWindow(glWindow->SDLWindow);
    delete glWindow;
    Window = nullptr;
  }
  if (Renderer != nullptr) {
    delete static_cast<OpenGL::Renderer*>(Renderer);
    Renderer = nullptr;
  }
  SDL_Quit();;

  Audio::HostAudioBridge::Get().Reset();
  st->GameOpened.store(false);
}

ImpactoHostResult DoDestroy(HostState* st) {
  if (st->GameOpened.load()) {
    TeardownEngine(st);
  }
  return IMPACTO_HOST_OK;
}

#endif  // !IMPACTO_DISABLE_OPENGL

HostState* HostFromVoid(void* host) {
  return static_cast<HostState*>(host);
}

}  // namespace

extern "C" {

int32_t impacto_host_create(const impacto_host_desc_t* desc, void** out_host) {
  if (desc == nullptr || out_host == nullptr) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (desc->struct_size < sizeof(impacto_host_desc_t) ||
      desc->api_version != IMPACTO_HOST_API_VERSION) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (desc->data_dir_utf8 == nullptr || desc->data_dir_utf8[0] == '\0' ||
      desc->writable_dir_utf8 == nullptr ||
      desc->writable_dir_utf8[0] == '\0') {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (desc->initial_width == 0 || desc->initial_height == 0) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
#ifdef IMPACTO_DISABLE_OPENGL
  return IMPACTO_HOST_NOT_SUPPORTED;
#endif
  std::lock_guard<std::mutex> instanceLock(g_InstanceMutex);
  if (g_Instance != nullptr) {
    // The engine is built around process-global state; one embedding at a
    // time.
    return IMPACTO_HOST_NOT_SUPPORTED;
  }

  auto* st = new HostState();
  {
    // Normalise early: the engine chdir()s during open, so relative paths
    // captured now must be anchored to the host process' cwd.
    std::error_code ec;
    auto dataDir = std::filesystem::absolute(desc->data_dir_utf8, ec);
    if (ec) {
      delete st;
      return IMPACTO_HOST_IO_ERROR;
    }
    auto writableDir = std::filesystem::absolute(desc->writable_dir_utf8, ec);
    if (ec) {
      delete st;
      return IMPACTO_HOST_IO_ERROR;
    }
    st->DataDir = dataDir.string();
    st->WritableDir = writableDir.string();
  }
  st->InitialWidth = desc->initial_width;
  if (const char* interval = std::getenv("IMPACTO_HOST_RENDER_INTERVAL")) {
    char* end = nullptr;
    long value = std::strtol(interval, &end, 10);
    if (end != interval && *end == '\0' && value >= 1 && value <= 120) {
      st->RenderInterval = static_cast<uint32_t>(value);
    }
  }
  st->InitialHeight = desc->initial_height;
  st->PendingWidth.store(desc->initial_width);
  st->PendingHeight.store(desc->initial_height);
  if (desc->log_level <= (uint32_t)Impacto::LogLevel::Max) {
    st->Level = (Impacto::LogLevel)desc->log_level;
  }

  std::error_code ec;
  std::filesystem::create_directories(st->WritableDir, ec);
  if (ec) {
    delete st;
    return IMPACTO_HOST_IO_ERROR;
  }

  Impacto::Host::HostBridge::Get().Reset();

  st->Thread = std::thread(EngineLoop, st);
  {
    // Wait for the loop to signal readiness.
    while (!st->EngineAlive.load()) {
      std::this_thread::yield();
    }
  }

  g_Instance = st;
  *out_host = st;
  return IMPACTO_HOST_OK;
}

void impacto_host_destroy(void* host) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return;

  {
    std::lock_guard<std::mutex> instanceLock(g_InstanceMutex);
    if (g_Instance != st) return;
    g_Instance = nullptr;
  }

  // Shut the engine down on its own thread, then stop the loop.
#ifndef IMPACTO_DISABLE_OPENGL
  PostCommand(st, [st] { return DoDestroy(st); });
#endif
  {
    std::lock_guard<std::mutex> lock(st->QueueMutex);
    st->QuitRequested = true;
  }
  st->QueueCV.notify_one();
  if (st->Thread.joinable()) st->Thread.join();

  delete st;
}

int32_t impacto_host_open_game(void* host, const char* game_root_utf8,
                               const char* game_id_utf8,
                               const char* gamedata_dir_utf8) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
  if (game_root_utf8 == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;

  std::string gameRoot = game_root_utf8;
  std::string gameId = game_id_utf8 ? game_id_utf8 : "";
  std::string gamedataDir = gamedata_dir_utf8 ? gamedata_dir_utf8 : "";

#ifdef IMPACTO_DISABLE_OPENGL
  (void)gameRoot;
  (void)gameId;
  (void)gamedataDir;
  return IMPACTO_HOST_NOT_SUPPORTED;
#else
  return PostCommand(st, [st, gameRoot, gameId, gamedataDir] {
    return DoOpenGame(st, gameRoot, gameId, gamedataDir);
  });
#endif
}

int32_t impacto_host_tick(void* host, uint32_t delta_ms) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
#ifdef IMPACTO_DISABLE_OPENGL
  (void)delta_ms;
  return IMPACTO_HOST_NOT_SUPPORTED;
#else
  return PostCommand(st, [st, delta_ms] { return DoTick(st, delta_ms); });
#endif
}

int32_t impacto_host_set_paused(void* host, int32_t paused) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
  st->Paused.store(paused != 0);
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_should_quit(void* host) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
  return st->ShouldQuit.load() ? 1 : 0;
}

int32_t impacto_host_get_frame_desc(void* host, uint32_t* out_width,
                                    uint32_t* out_height,
                                    uint32_t* out_stride_bytes) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || out_width == nullptr || out_height == nullptr ||
      out_stride_bytes == nullptr) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;
  auto& bridge = Impacto::Host::HostBridge::Get();
  bridge.GetFrameDesc(*out_width, *out_height, *out_stride_bytes);
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_read_frame_rgba(void* host, void* out_pixels,
                                     size_t out_pixels_size) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || out_pixels == nullptr) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;
#ifndef IMPACTO_DISABLE_OPENGL
  if (st->RenderInterval > 1) {
    auto result = PostCommand(st, [st] {
      if (st->RenderPending) RenderAndCapture(st);
      return IMPACTO_HOST_OK;
    });
    if (result != IMPACTO_HOST_OK) return result;
  }
#endif
  if (!Impacto::Host::HostBridge::Get().CopyFrame(out_pixels,
                                                  out_pixels_size)) {
    return IMPACTO_HOST_INVALID_STATE;
  }
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_get_audio_format(void* host, uint32_t* out_sample_rate,
                                      uint32_t* out_channel_count) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || out_sample_rate == nullptr ||
      out_channel_count == nullptr) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;
  auto& bridge = Impacto::Audio::HostAudioBridge::Get();
  *out_sample_rate = bridge.SampleRate();
  *out_channel_count = Impacto::Audio::HostAudioBridge::kOutputChannels;
  return IMPACTO_HOST_OK;
}

int64_t impacto_host_read_audio_f32(void* host, float* out_samples,
                                    int64_t capacity_samples) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || out_samples == nullptr || capacity_samples <= 0) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;
  return Impacto::Audio::HostAudioBridge::Get().ReadSamples(
      out_samples, capacity_samples);
}

int32_t impacto_host_get_audio_stats(void* host, uint64_t* out_frames_written,
                                     uint64_t* out_frames_dropped) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || out_frames_written == nullptr ||
      out_frames_dropped == nullptr) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  Impacto::Audio::HostAudioBridge::Get().GetStats(*out_frames_written,
                                                  *out_frames_dropped);
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_send_key(void* host, uint32_t sdl_scancode,
                              int32_t down) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;

  SDL_Event event{};
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.down = down != 0;
  event.key.scancode = static_cast<SDL_Scancode>(sdl_scancode);
  event.key.key = 0;
  event.key.mod = SDL_KMOD_NONE;
  event.key.repeat = false;
  if (!SDL_PushEvent(&event)) return IMPACTO_HOST_INTERNAL_ERROR;
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_send_pointer(void* host, double x, double y,
                                  int32_t button, int32_t down) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;

  SDL_Event event{};

  if (down == -1) {
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = (float)x;
    event.motion.y = (float)y;
  } else {
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.x = (float)x;
    event.button.y = (float)y;
    event.button.button = static_cast<Uint8>(button);
    event.button.down = down != 0;
    event.button.clicks = 1;
  }
  if (!SDL_PushEvent(&event)) return IMPACTO_HOST_INTERNAL_ERROR;
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_send_wheel(void* host, double dx, double dy) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return IMPACTO_HOST_INVALID_ARGUMENT;
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;

  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_WHEEL;
  event.wheel.x = (float)dx;
  event.wheel.y = (float)dy;
  if (!SDL_PushEvent(&event)) return IMPACTO_HOST_INTERNAL_ERROR;
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_send_text(void* host, const char* utf8_text) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || utf8_text == nullptr) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  if (!st->GameOpened.load()) return IMPACTO_HOST_INVALID_STATE;

  /* SDL3's TEXT_INPUT event carries a const char* into storage owned by the
   * sender, and the event may sit in SDL's queue until the engine thread
   * polls it. Keep a small ring of slots so the last few texts stay valid
   * while queued; text input is far too slow to wrap the ring before the
   * engine (which polls every tick) consumes the event. */
  constexpr size_t kTextInputSlotBytes = 32;  // UTF-8, one scalar value max
  static std::mutex textPoolMutex;
  static char textPool[8][kTextInputSlotBytes];
  static size_t textPoolIndex = 0;

  char* slot = nullptr;
  {
    std::lock_guard<std::mutex> lock(textPoolMutex);
    slot = textPool[textPoolIndex];
    textPoolIndex = (textPoolIndex + 1) % 8;
  }
  const size_t maxLength = kTextInputSlotBytes - 1;
  const size_t length = std::min(std::strlen(utf8_text), maxLength);
  std::memcpy(slot, utf8_text, length);
  slot[length] = '\0';

  SDL_Event event{};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text.text = slot;
  if (!SDL_PushEvent(&event)) return IMPACTO_HOST_INTERNAL_ERROR;
  return IMPACTO_HOST_OK;
}

int32_t impacto_host_surface_size(void* host, uint32_t width, uint32_t height) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr || width == 0 || height == 0) {
    return IMPACTO_HOST_INVALID_ARGUMENT;
  }
  st->PendingWidth.store(width);
  st->PendingHeight.store(height);
  if (st->GameOpened.load()) {
    return IMPACTO_HOST_INVALID_STATE;
  }
  return IMPACTO_HOST_OK;
}

const char* impacto_host_get_last_error(void* host) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return nullptr;
  static thread_local std::string error;
  error = Impacto::Host::HostBridge::Get().GetLastError();
  return error.empty() ? nullptr : error.c_str();
}

const char* impacto_host_get_renderer_info(void* host) {
  HostState* st = HostFromVoid(host);
  if (st == nullptr) return nullptr;
  static thread_local std::string info;
  info = Impacto::Host::HostBridge::Get().GetRendererInfo();
  return info.empty() ? nullptr : info.c_str();
}

}  // extern "C"
